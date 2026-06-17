#include "NativeLogger.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

static constexpr char kNativeLoggerTag[] = "NativeLogger";
static constexpr char kSpdlogLoggerName[] = "native_file_logger";

static constexpr char kAppLogDirName[] = "logs";
static constexpr char kAppLogFileName[] = "app.log";
static constexpr char kDatasetLogFileName[] = "capture.log";
static constexpr char kFileLogPattern[] = "[%l] %v";

static constexpr auto kFlushInterval = std::chrono::seconds(3);

NativeLogger& NativeLogger::Instance() {
    static NativeLogger logger;
    return logger;
}

NativeLogger::NativeLogger() = default;

NativeLogger::~NativeLogger() {
    Shutdown();
}

bool NativeLogger::Init(const char* filesDir,
                        std::size_t maxFileSizeBytes,
                        std::size_t maxRotatedFiles) {
    const std::string appLogDir =
            std::string(filesDir) + "/" + kAppLogDirName;

    if (mkdir(appLogDir.c_str(), 0755) != 0 && errno != EEXIST) {
        EmitInternalError("NativeLogger::Init",
                          "failed to create logs directory");
        return false;
    }

    const std::string appLogFilePath =
            appLogDir + "/" + kAppLogFileName;

    spdlog::sink_ptr newAppSink;
    std::shared_ptr<spdlog::logger> newLogger;

    try {
        newAppSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                appLogFilePath,
                maxFileSizeBytes,
                maxRotatedFiles,
                false);

        newLogger = MakeLogger(newAppSink);
    } catch (const std::exception& ex) {
        EmitInternalError("NativeLogger::Init", ex.what());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        ResetLoggerLocked();

        appSink_ = newAppSink;
        logger_ = newLogger;

        StartFlushThreadLocked();
    }

    return true;
}

bool NativeLogger::StartDataset(const char* datasetDir,
                                std::size_t maxFileSizeBytes,
                                std::size_t maxRotatedFiles) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!logger_) {
            EmitInternalError("NativeLogger::StartDataset",
                              "NativeLogger::Init must be called before StartDataset");
            return false;
        }
    }
    const std::string datasetLogFilePath =
            std::string(datasetDir) + "/" + kDatasetLogFileName;

    spdlog::sink_ptr newDatasetSink;

    try {
        newDatasetSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                datasetLogFilePath,
                maxFileSizeBytes,
                maxRotatedFiles,
                false);
        newDatasetSink->set_pattern(kFileLogPattern);
    } catch (const std::exception& ex) {
        EmitInternalError("NativeLogger::Init", ex.what());
        return false;
    }

    RequestFlushAndWait();

    if (datasetSink_) {
        RemoveSink(logger_, datasetSink_);
        datasetSink_.reset();
    }

    datasetSink_ = newDatasetSink;
    logger_->sinks().push_back(datasetSink_);

    return true;
}

void NativeLogger::StopDataset() {
    RequestFlushAndWait();

    std::lock_guard<std::mutex> lock(mutex_);

    if (!logger_ || !datasetSink_) {
        return;
    }

    datasetSink_->flush();

    RemoveSink(logger_, datasetSink_);
    datasetSink_.reset();
}

void NativeLogger::Shutdown() {
    std::thread localThread;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!flushThreadRunning_) {
            ResetLoggerLocked();
            return;
        }

        ++flushRequestSeq_;
        flushRequested_ = true;
        stopFlushThread_ = true;

        cv_.notify_all();

        localThread = std::move(flushThread_);
    }

    if (localThread.joinable())
        localThread.join();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        stopFlushThread_ = false;
        flushRequested_ = false;
        flushThreadRunning_ = false;

        ResetLoggerLocked();

        cv_.notify_all();
    }
}

void NativeLogger::VPrint(int prio, const char* tag, const char* fmt, va_list args) {
    va_list androidArgs;
    va_copy(androidArgs, args);
    __android_log_vprint(prio, tag, fmt, androidArgs);
    va_end(androidArgs);

    const std::string message = FormatMessage(fmt, args);
    if (message.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!logger_) {
        return;
    }

    logger_->log(ToSpdlogLevel(prio),
                 "boot_ms={} {} {}",
                 CurrentBootTimeMs(),
                 tag,
                 message);
}

void NativeLogger::StartFlushThreadLocked() {
    if (flushThreadRunning_) {
        return;
    }

    stopFlushThread_ = false;
    flushRequested_ = false;
    flushThreadRunning_ = true;

    flushThread_ = std::thread(&NativeLogger::FlushLoop, this);
}

void NativeLogger::FlushLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait_for(lock, kFlushInterval, [this]() {
            return stopFlushThread_ || flushRequested_;
        });

        const bool shouldStop = stopFlushThread_;
        const std::uint64_t targetSeq = flushRequestSeq_;

        flushRequested_ = false;

        if (logger_) {
            logger_->flush();
        }

        if (flushDoneSeq_ < targetSeq) {
            flushDoneSeq_ = targetSeq;
        }

        cv_.notify_all();

        if (shouldStop) {
            flushThreadRunning_ = false;
            cv_.notify_all();
            break;
        }
    }
}

void NativeLogger::RequestFlushAndWait() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!flushThreadRunning_) {
        if (!logger_) {
            return;
        }
        logger_->flush();
        return;
    }

    const std::uint64_t targetSeq = ++flushRequestSeq_;
    flushRequested_ = true;

    cv_.notify_all();

    cv_.wait(lock, [this, targetSeq]() {
        return flushDoneSeq_ >= targetSeq || !flushThreadRunning_;
    });
}

void NativeLogger::ResetLoggerLocked() {
    if (!logger_) {
        return;
    }
    logger_->flush();

    if (logger_) {
        logger_->sinks().clear();
    }

    datasetSink_.reset();
    appSink_.reset();
    logger_.reset();
}

void NativeLogger::EmitInternalError(const char* where, const char* detail) {
    __android_log_print(ANDROID_LOG_ERROR,
                        kNativeLoggerTag,
                        "%s: %s",
                        where,
                        detail);
}

int64_t NativeLogger::CurrentBootTimeMs() {
    timespec ts {};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
        return 0;
    }

    return static_cast<int64_t>(ts.tv_sec) * 1000LL +
           static_cast<int64_t>(ts.tv_nsec) / 1000000LL;
}

spdlog::level::level_enum NativeLogger::ToSpdlogLevel(int prio) {
    switch (prio) {
        case ANDROID_LOG_VERBOSE:
            return spdlog::level::trace;
        case ANDROID_LOG_DEBUG:
            return spdlog::level::debug;
        case ANDROID_LOG_INFO:
            return spdlog::level::info;
        case ANDROID_LOG_WARN:
            return spdlog::level::warn;
        case ANDROID_LOG_ERROR:
            return spdlog::level::err;
        case ANDROID_LOG_FATAL:
            return spdlog::level::critical;
        default:
            return spdlog::level::info;
    }
}

std::string NativeLogger::FormatMessage(const char* fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);

    const int length = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);

    if (length <= 0) {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(length) + 1U);

    va_list copy2;
    va_copy(copy2, args);
    vsnprintf(buffer.data(), buffer.size(), fmt, copy2);
    va_end(copy2);

    return std::string(buffer.data(), static_cast<std::size_t>(length));
}


std::shared_ptr<spdlog::logger> NativeLogger::MakeLogger(const spdlog::sink_ptr& appSink) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(appSink);

    auto logger = std::make_shared<spdlog::logger>(
            kSpdlogLoggerName,
            sinks.begin(),
            sinks.end());

    logger->set_level(spdlog::level::trace);

    // 文件格式：
    // [info] boot_ms=123456 TAG message
    logger->set_pattern(kFileLogPattern);

    logger->set_error_handler([](const std::string& msg) {
        EmitInternalError("spdlog", msg.c_str());
    });

    return logger;
}

void NativeLogger::RemoveSink(std::shared_ptr<spdlog::logger>& logger,
                              const spdlog::sink_ptr& sink) {
    if (!logger || !sink) {
        return;
    }

    auto& sinks = logger->sinks();
    sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
}


bool NativeLoggerInit(const char* filesDir,
                      std::size_t maxFileSizeBytes,
                      std::size_t maxRotatedFiles) {
    return NativeLogger::Instance().Init(filesDir,
                                         maxFileSizeBytes,
                                         maxRotatedFiles);
}

bool NativeLoggerStartDataset(const char* datasetDir,
                              std::size_t maxFileSizeBytes,
                              std::size_t maxRotatedFiles) {
    return NativeLogger::Instance().StartDataset(datasetDir,
                                                 maxFileSizeBytes,
                                                 maxRotatedFiles);
}

void NativeLoggerStopDataset() {
    NativeLogger::Instance().StopDataset();
}

void NativeLoggerShutdown() {
    NativeLogger::Instance().Shutdown();
}

void NativeLogPrint(int prio, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    NativeLogger::Instance().VPrint(prio, tag, fmt, args);
    va_end(args);
}