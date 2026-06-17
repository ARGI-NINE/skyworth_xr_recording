#pragma once

#include <android/log.h>

#include <cstddef>
#include <cstdarg>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

constexpr std::size_t kNativeLoggerDefaultMaxFileSizeBytes = 32U * 1024U * 1024U;
constexpr std::size_t kNativeLoggerDefaultMaxRotatedFiles = 1U;

class NativeLogger final {
public:
    static NativeLogger& Instance();

    NativeLogger(const NativeLogger&) = delete;
    NativeLogger& operator=(const NativeLogger&) = delete;

    bool Init(const char* filesDir,
              std::size_t maxFileSizeBytes = kNativeLoggerDefaultMaxFileSizeBytes,
              std::size_t maxRotatedFiles = kNativeLoggerDefaultMaxRotatedFiles);

    bool StartDataset(const char* datasetDir,
                      std::size_t maxFileSizeBytes = kNativeLoggerDefaultMaxFileSizeBytes,
                      std::size_t maxRotatedFiles = kNativeLoggerDefaultMaxRotatedFiles);

    void StopDataset();

    void Shutdown();

    void VPrint(int prio, const char* tag, const char* fmt, va_list args);

private:
    NativeLogger();
    ~NativeLogger();

    void StartFlushThreadLocked();
    void FlushLoop();
    void RequestFlushAndWait();
    void ResetLoggerLocked();

    static void EmitInternalError(const char* where, const char* detail);

    static int64_t CurrentBootTimeMs();

    static spdlog::level::level_enum ToSpdlogLevel(int prio);

    static std::string FormatMessage(const char* fmt, va_list args);

    static std::shared_ptr<spdlog::logger> MakeLogger(const spdlog::sink_ptr& appSink);

    static void RemoveSink(std::shared_ptr<spdlog::logger>& logger,
                           const spdlog::sink_ptr& sink);

private:
    std::mutex mutex_;
    std::condition_variable cv_;

    std::thread flushThread_;
    bool flushThreadRunning_ = false;
    bool stopFlushThread_ = false;
    bool flushRequested_ = false;

    std::uint64_t flushRequestSeq_ = 0;
    std::uint64_t flushDoneSeq_ = 0;

    std::shared_ptr<spdlog::logger> logger_;
    spdlog::sink_ptr appSink_;
    spdlog::sink_ptr datasetSink_;
};

bool NativeLoggerInit(const char* filesDir,
                      std::size_t maxFileSizeBytes = kNativeLoggerDefaultMaxFileSizeBytes,
                      std::size_t maxRotatedFiles = kNativeLoggerDefaultMaxRotatedFiles);

bool NativeLoggerStartDataset(const char* datasetDir,
                              std::size_t maxFileSizeBytes = kNativeLoggerDefaultMaxFileSizeBytes,
                              std::size_t maxRotatedFiles = kNativeLoggerDefaultMaxRotatedFiles);

void NativeLoggerStopDataset();

void NativeLoggerShutdown();

void NativeLogPrint(int prio, const char* tag, const char* fmt, ...)
__attribute__((__format__(printf, 3, 4)));

#define NATIVE_LOGI(TAG, ...) ((void)NativeLogPrint(ANDROID_LOG_INFO, TAG, __VA_ARGS__))
#define NATIVE_LOGW(TAG, ...) ((void)NativeLogPrint(ANDROID_LOG_WARN, TAG, __VA_ARGS__))
#define NATIVE_LOGE(TAG, ...) ((void)NativeLogPrint(ANDROID_LOG_ERROR, TAG, __VA_ARGS__))
#define NATIVE_LOGD(TAG, ...) ((void)NativeLogPrint(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__))