#include "DatasetRecorder.h"
#include "RawDateSave.h"

#include <android/log.h>
#include <sys/stat.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#define LOG_TAG "DatasetRecorder"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

int64_t DatasetRecorder::currentUnixTimeMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

std::string DatasetRecorder::formatUnixTimeMs(int64_t unixTimeMs) {
    if (unixTimeMs <= 0) return "";

    std::time_t seconds = static_cast<std::time_t>(unixTimeMs / 1000);
    struct tm tm_buf;
    localtime_r(&seconds, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

DatasetRecorder::DatasetRecorder() = default;

DatasetRecorder::~DatasetRecorder() {
    stop();
}

void DatasetRecorder::init(const std::string& basePath) {
    mBasePath = basePath;
    LOGI("DatasetRecorder initialized with basePath: %s", basePath.c_str());
}

std::string DatasetRecorder::generateDatasetDirName() {
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_r(&time_t_val, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    return ss.str();
}

bool DatasetRecorder::start() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (mRecording.load()) {
        LOGW("Already recording");
        return false;
    }

    std::string datasetBase = mBasePath + "/dataset";
    mkdir(datasetBase.c_str(), 0777);

    std::string dirName = generateDatasetDirName();
    mDatasetDir = datasetBase + "/" + dirName;
    if (mkdir(mDatasetDir.c_str(), 0777) != 0) {
        LOGE("Failed to create dataset directory: %s", mDatasetDir.c_str());
        return false;
    }

    LOGI("Dataset recording started: %s", mDatasetDir.c_str());
    mCaptureStartUnixMs = currentUnixTimeMs();
    mCaptureStopUnixMs = 0;

    // Start audio encoder
    std::string audioPath = mDatasetDir + "/audio.m4a";
    if (!mAudioEncoder.start(audioPath)) {
        LOGE("Failed to start audio encoder");
    }

    // Start IMU collector (accel.csv + gyro.csv)
    std::string accelPath = mDatasetDir + "/accel.csv";
    std::string gyroPath = mDatasetDir + "/gyro.csv";
    if (!mImuCollector.start(accelPath, gyroPath)) {
        LOGE("Failed to start IMU collector");
    }

    // Start head pose writer (head_pose.csv)
    mPoseCount = 0;
    mPoseWriterFinished = false;
    mPoseFile.open(mDatasetDir + "/head_pose.csv", std::ios::out | std::ios::trunc);
    if (mPoseFile.is_open()) {
        mPoseFile << "timestamp_ns,pos_x,pos_y,pos_z,quat_x,quat_y,quat_z,quat_w\n";
        mPoseFile.flush();
        mPoseWriterRunning = true;
        mPoseWriterThread = std::thread(&DatasetRecorder::poseWriterThreadFunc, this);
    } else {
        LOGE("Failed to open head_pose.csv");
    }

    // Start BOOTTIME→REALTIME offset sampling (1 Hz)
    mTimeOffsets.clear();
    mTimeOffsetFinished = false;
    mOffsetRunning = true;
    mOffsetThread = std::thread(&DatasetRecorder::offsetSamplingThreadFunc, this);

    mRecording = true;
    return true;
}

void DatasetRecorder::stop() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mRecording.load()) return;

    LOGI("Dataset recording stopping: %s", mDatasetDir.c_str());
    if (mCaptureStopUnixMs <= 0) {
        mCaptureStopUnixMs = currentUnixTimeMs();
    }

    mAudioEncoder.stop();
    mImuCollector.stop();

    // Stop pose writer
    mPoseWriterRunning = false;
    mPoseCV.notify_all();
    if (mPoseWriterThread.joinable()) mPoseWriterThread.join();

    // Stop offset sampling
    mOffsetRunning = false;
    if (mOffsetThread.joinable()) mOffsetThread.join();
    writeTimeOffsetJson();

    // Flush remaining pose entries
    {
        std::lock_guard<std::mutex> plock(mPoseMutex);
        while (!mPoseQueue.empty()) {
            auto& e = mPoseQueue.front();
            mPoseFile << e.timestamp
                << "," << e.pos[0] << "," << e.pos[1] << "," << e.pos[2]
                << "," << e.quat[0] << "," << e.quat[1] << "," << e.quat[2] << "," << e.quat[3] << "\n";
            mPoseQueue.pop();
        }
    }

    if (mPoseFile.is_open()) { mPoseFile.flush(); mPoseFile.close(); }
    mPoseWriterFinished = true;
    mTimeOffsetFinished = true;

    mRecording = false;
    LOGI("Dataset recording stopped. Poses: %lu", (unsigned long)mPoseCount.load());
}

std::string DatasetRecorder::getHandTrackingCsvPath() const {
    if (mDatasetDir.empty()) return "";
    return mDatasetDir + "/hand_tracking.csv";
}

std::string DatasetRecorder::getControllerPoseCsvPath() const {
    if (mDatasetDir.empty()) return "";
    return mDatasetDir + "/controller_poses.csv";
}

std::string DatasetRecorder::getAudioPath() const {
    if (mDatasetDir.empty()) return "";
    return mDatasetDir + "/audio.m4a";
}

bool DatasetRecorder::writeCaptureStatusJson(const std::string& state, const RawDateSave* handSaver) const {
    if (mDatasetDir.empty()) {
        LOGE("writeCaptureStatusJson: dataset dir is empty");
        return false;
    }

    const bool audioFinished = mAudioEncoder.isFinished();
    const bool imuFinished = mImuCollector.isFinished();
    const bool headPoseFinished = mPoseWriterFinished.load();
    const bool timeOffsetFinished = mTimeOffsetFinished.load();
    const bool handTrackingFinished = handSaver && handSaver->IsFinished();

    const bool allChainsFinished =
        audioFinished &&
        imuFinished &&
        headPoseFinished &&
        timeOffsetFinished &&
        handTrackingFinished;

    std::string captureState;
    if (state == "recording") {
        captureState = "recording";
    } else if (allChainsFinished) {
        captureState = "complete";
    } else {
        captureState = "finalizing";
    }

    const int64_t nowMs = currentUnixTimeMs();
    const bool captureEnded = captureState != "recording";
    const int64_t endedAtUnixMs = mCaptureStopUnixMs > 0 ? mCaptureStopUnixMs : (captureEnded ? nowMs : 0);
    const int64_t durationMs = (mCaptureStartUnixMs > 0)
        ? ((endedAtUnixMs > 0 ? endedAtUnixMs : nowMs) - mCaptureStartUnixMs)
        : 0;

    const std::string jsonPath = mDatasetDir + "/capture_status.json";
    std::ofstream f(jsonPath, std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        LOGE("Failed to open capture_status.json");
        return false;
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"state\": \"" << captureState << "\",\n"
         << "  \"dataset_dir\": \"" << mDatasetDir << "\",\n"
         << "  \"started_time_at_local\": \"" << formatUnixTimeMs(mCaptureStartUnixMs) << "\",\n"
         << "  \"ended_time_at_local\": \"" << formatUnixTimeMs(endedAtUnixMs) << "\",\n"
         << "  \"duration_ms\": " << durationMs << "\n"
         << "}\n";

    f << json.str();
    f.close();

    LOGI("capture_status.json written: %s state=%s",
         jsonPath.c_str(), captureState.c_str());
    return true;
}

void DatasetRecorder::saveHeadPose(int64_t boottimeNs, const XrPosef& pose) {
    if (!mRecording.load() || !mPoseWriterRunning.load()) return;

    PoseEntry entry{};
    entry.timestamp = boottimeNs;
    entry.pos[0] = pose.position.x;
    entry.pos[1] = pose.position.y;
    entry.pos[2] = pose.position.z;
    entry.quat[0] = pose.orientation.x;
    entry.quat[1] = pose.orientation.y;
    entry.quat[2] = pose.orientation.z;
    entry.quat[3] = pose.orientation.w;

    {
        std::lock_guard<std::mutex> lock(mPoseMutex);
        mPoseQueue.push(entry);
    }
    mPoseCV.notify_one();
}

void DatasetRecorder::poseWriterThreadFunc() {
    LOGI("Pose writer thread started");

    while (mPoseWriterRunning.load() || !mPoseQueue.empty()) {
        PoseEntry entry;
        bool hasEntry = false;

        {
            std::unique_lock<std::mutex> lock(mPoseMutex);
            mPoseCV.wait_for(lock, std::chrono::milliseconds(2), [this] {
                return !mPoseQueue.empty() || !mPoseWriterRunning.load();
            });
            if (!mPoseQueue.empty()) {
                entry = mPoseQueue.front();
                mPoseQueue.pop();
                hasEntry = true;
            }
        }

        if (hasEntry) {
            mPoseFile << entry.timestamp
                << "," << entry.pos[0] << "," << entry.pos[1] << "," << entry.pos[2]
                << "," << entry.quat[0] << "," << entry.quat[1] << "," << entry.quat[2] << "," << entry.quat[3] << "\n";
            mPoseCount++;
            if (mPoseCount % 100 == 0) mPoseFile.flush();
        }
    }

    if (mPoseFile.is_open()) mPoseFile.flush();
    LOGI("Pose writer thread exited, total: %lu", (unsigned long)mPoseCount.load());
}

void DatasetRecorder::offsetSamplingThreadFunc() {
    LOGI("Offset sampling thread started (1 Hz)");

    while (mOffsetRunning.load()) {
        struct timespec bt, rt;
        clock_gettime(CLOCK_BOOTTIME, &bt);
        clock_gettime(CLOCK_REALTIME, &rt);

        TimeOffsetSample sample;
        sample.boottimeNs = (int64_t)bt.tv_sec * 1000000000LL + bt.tv_nsec;
        sample.realtimeNs = (int64_t)rt.tv_sec * 1000000000LL + rt.tv_nsec;
        sample.offsetNs = sample.realtimeNs - sample.boottimeNs;

        {
            std::lock_guard<std::mutex> lock(mOffsetMutex);
            mTimeOffsets.push_back(sample);
        }

        // Sleep 1 second (fractional sleep to avoid drift accumulation)
        auto next = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (mOffsetRunning.load() && std::chrono::steady_clock::now() < next) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    LOGI("Offset sampling thread exited, samples: %zu", mTimeOffsets.size());
}

void DatasetRecorder::writeTimeOffsetJson() {
    std::lock_guard<std::mutex> lock(mOffsetMutex);
    if (mTimeOffsets.empty()) return;

    std::string path = mDatasetDir + "/time_offset.json";
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        LOGE("Failed to open time_offset.json");
        return;
    }

    f << "{\n";
    f << "  \"description\": \"CLOCK_BOOTTIME to CLOCK_REALTIME (UTC) offset samples\",\n";
    f << "  \"unit\": \"nanoseconds\",\n";
    f << "  \"formula\": \"utc_timestamp_ns = boottime_timestamp_ns + offset_ns\",\n";
    f << "  \"offsets\": [\n";

    for (size_t i = 0; i < mTimeOffsets.size(); ++i) {
        const auto& s = mTimeOffsets[i];
        f << "    {"
          << "\"boottime_ns\": " << s.boottimeNs
          << ", \"realtime_ns\": " << s.realtimeNs
          << ", \"offset_ns\": " << s.offsetNs << "}";
        if (i < mTimeOffsets.size() - 1) f << ",";
        f << "\n";
    }

    f << "  ]\n";
    f << "}\n";
    f.close();

    LOGI("time_offset.json written: %zu offset samples", mTimeOffsets.size());
}
