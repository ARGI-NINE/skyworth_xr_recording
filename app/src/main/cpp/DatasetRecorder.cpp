#include "DatasetRecorder.h"
#include "RawDateSave.h"
#include "NativeLogger.h"

#include <sys/stat.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdint>


#define LOG_TAG "DatasetRecorder"
#define LOGI(...) NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGW(...) NATIVE_LOGW(LOG_TAG, __VA_ARGS__)
#define LOGE(...) NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

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

    NativeLoggerStartDataset(mDatasetDir.c_str());
    LOGI("Dataset recording started: %s", mDatasetDir.c_str());
    mCaptureStartUnixMs = currentUnixTimeMs();
    mCaptureStopUnixMs = 0;

    // Capture BOOTTIME→REALTIME offset once at recording start
    {
        struct timespec bt, rt;
        clock_gettime(CLOCK_BOOTTIME, &bt);
        clock_gettime(CLOCK_REALTIME, &rt);
        int64_t boottimeNs = (int64_t)bt.tv_sec * 1000000000LL + bt.tv_nsec;
        int64_t realtimeNs = (int64_t)rt.tv_sec * 1000000000LL + rt.tv_nsec;
        mBoottimeToRealtimeOffsetNs = realtimeNs - boottimeNs;
    }
    LOGI("BOOTTIME→REALTIME offset: %ld ns", (long)mBoottimeToRealtimeOffsetNs);

    // Start audio encoder (set offset BEFORE start to avoid race window)
    std::string audioPath = mDatasetDir + "/audio.m4a";
    mAudioEncoder.setTimeOffset(mBoottimeToRealtimeOffsetNs);
    if (!mAudioEncoder.start(audioPath)) {
        LOGE("Failed to start audio encoder");
    }

    // Start IMU collector (set offset BEFORE start to avoid race window)
    std::string accelPath = mDatasetDir + "/accel.csv";
    std::string gyroPath = mDatasetDir + "/gyro.csv";
    mImuCollector.setTimeOffset(mBoottimeToRealtimeOffsetNs);
    if (!mImuCollector.start(accelPath, gyroPath)) {
        LOGE("Failed to start IMU collector");
    }

    // Start head pose writer (head_pose.csv)
    mPoseCount = 0;
    mPoseWriterFinished = false;
    mMaxSeenPoseTimestamp = 0;
    mPoseMap.clear();
    mPoseFile.open(mDatasetDir + "/head_pose.csv", std::ios::out | std::ios::trunc);
    if (mPoseFile.is_open()) {
        mPoseFile << "timestamp_ns,pos_x,pos_y,pos_z,quat_x,quat_y,quat_z,quat_w\n";
        mPoseFile.flush();
        mPoseWriterRunning = true;
        mPoseWriterThread = std::thread(&DatasetRecorder::poseWriterThreadFunc, this);
    } else {
        LOGE("Failed to open head_pose.csv");
    }

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

    // Flush remaining pose entries (already sorted by timestamp)
    {
        std::lock_guard<std::mutex> plock(mPoseMutex);
        for (const auto& entry : mPoseMap) {
            const auto& e = entry.second;
            int64_t utcNs = e.timestamp + mBoottimeToRealtimeOffsetNs;
            mPoseFile << utcNs
                << "," << e.pos[0] << "," << e.pos[1] << "," << e.pos[2]
                << "," << e.quat[0] << "," << e.quat[1] << "," << e.quat[2] << "," << e.quat[3] << "\n";
        }
        mPoseMap.clear();
    }

    if (mPoseFile.is_open()) { mPoseFile.flush(); mPoseFile.close(); }
    mPoseWriterFinished = true;

    mRecording = false;
    LOGI("Dataset recording stopped. Poses: %lu", (unsigned long)mPoseCount.load());
    NativeLoggerStopDataset();
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
    const bool handTrackingFinished = handSaver && handSaver->IsFinished();

    const bool allChainsFinished =
        audioFinished &&
        imuFinished &&
        headPoseFinished &&
        handTrackingFinished;

    std::string captureState;
    if (state == "recording") {
        captureState = "recording";
    } else if (allChainsFinished) {
        captureState = "complete";
    } else {
        captureState = "finalizing";
    }

    const int64_t captureDurationMs =
        (mCaptureStartUnixMs > 0 && mCaptureStopUnixMs > 0)
            ? (mCaptureStopUnixMs - mCaptureStartUnixMs)
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
         << "  \"capture_started_at_local\": \"" << formatUnixTimeMs(mCaptureStartUnixMs) << "\",\n"
         << "  \"capture_ended_at_local\": \"" << formatUnixTimeMs(mCaptureStopUnixMs) << "\",\n"
         << "  \"capture_duration_ms\": " << captureDurationMs << "\n"
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
        mPoseMap[entry.timestamp] = entry;
        if (entry.timestamp > mMaxSeenPoseTimestamp) {
            mMaxSeenPoseTimestamp = entry.timestamp;
        }
    }
    mPoseCV.notify_one();
}

void DatasetRecorder::poseWriterThreadFunc() {
    LOGI("Pose writer thread started");

    while (true) {
        bool shouldExit = false;
        {
            std::unique_lock<std::mutex> lock(mPoseMutex);
            if (mPoseMap.empty()) {
                if (!mPoseWriterRunning.load()) {
                    shouldExit = true;
                } else {
                    mPoseCV.wait_for(lock, std::chrono::milliseconds(2));
                    continue;
                }
            }
        }
        if (shouldExit) break;

        int written = 0;
        {
            std::lock_guard<std::mutex> lock(mPoseMutex);
            int64_t safeThreshold;
            if (!mPoseWriterRunning.load()) {
                safeThreshold = INT64_MAX;
            } else {
                safeThreshold = mMaxSeenPoseTimestamp - REORDER_WINDOW_NS;
            }

            auto it = mPoseMap.begin();
            while (it != mPoseMap.end() && it->first <= safeThreshold) {
                const auto& e = it->second;
                int64_t utcNs = e.timestamp + mBoottimeToRealtimeOffsetNs;
                mPoseFile << utcNs
                    << "," << e.pos[0] << "," << e.pos[1] << "," << e.pos[2]
                    << "," << e.quat[0] << "," << e.quat[1] << "," << e.quat[2] << "," << e.quat[3] << "\n";
                mPoseCount++;
                it = mPoseMap.erase(it);
                written++;
            }
        }

        if (written > 0 && (mPoseCount.load() % 100) < (uint64_t)written) {
            mPoseFile.flush();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mPoseMutex);
        for (const auto& entry : mPoseMap) {
            const auto& e = entry.second;
            int64_t utcNs = e.timestamp + mBoottimeToRealtimeOffsetNs;
            mPoseFile << utcNs
                << "," << e.pos[0] << "," << e.pos[1] << "," << e.pos[2]
                << "," << e.quat[0] << "," << e.quat[1] << "," << e.quat[2] << "," << e.quat[3] << "\n";
            mPoseCount++;
        }
        mPoseMap.clear();
    }

    if (mPoseFile.is_open()) mPoseFile.flush();
    LOGI("Pose writer thread exited, total: %lu", (unsigned long)mPoseCount.load());
}
