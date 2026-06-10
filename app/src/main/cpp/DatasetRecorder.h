#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <fstream>

#include "ImuPoseCollector.h"
#include "AudioEncoder.h"
#include "openxr/openxr.h"

class RawDateSave;

class DatasetRecorder {
public:
    DatasetRecorder();
    ~DatasetRecorder();

    // Initialize with base storage path
    void init(const std::string& basePath);

    // Start recording: creates dataset_<timestamp> directory, starts collectors
    bool start();

    // Stop recording: stops all collectors, flushes data
    void stop();

    bool isRecording() const { return mRecording.load(); }
    const std::string& getDatasetDir() const { return mDatasetDir; }

    // Get paths for other collectors
    std::string getHandTrackingCsvPath() const;
    std::string getControllerPoseCsvPath() const;
    std::string getAudioPath() const;
    bool writeCaptureStatusJson(const std::string& state, const RawDateSave* handSaver) const;

    // Save head pose from render thread (async, non-blocking)
    void saveHeadPose(int64_t boottimeNs, const XrPosef& pose);

private:
    void poseWriterThreadFunc();
    void offsetSamplingThreadFunc();
    void writeTimeOffsetJson();

    std::string mBasePath;
    std::string mDatasetDir;
    std::atomic<bool> mRecording{false};
    std::mutex mMutex;

    // IMU collector
    ImuPoseCollector mImuCollector;

    // Audio encoder
    AudioEncoder mAudioEncoder;

    // Head pose async writer
    struct PoseEntry {
        int64_t timestamp;
        float pos[3];
        float quat[4];
    };
    std::queue<PoseEntry> mPoseQueue;
    std::mutex mPoseMutex;
    std::condition_variable mPoseCV;
    std::thread mPoseWriterThread;
    std::ofstream mPoseFile;
    std::atomic<bool> mPoseWriterRunning{false};
    std::atomic<bool> mPoseWriterFinished{false};
    std::atomic<uint64_t> mPoseCount{0};

    // BOOTTIME → REALTIME offset sampling (1 Hz)
    struct TimeOffsetSample {
        int64_t boottimeNs;
        int64_t realtimeNs;
        int64_t offsetNs;
    };
    std::vector<TimeOffsetSample> mTimeOffsets;
    std::mutex mOffsetMutex;
    std::thread mOffsetThread;
    std::atomic<bool> mOffsetRunning{false};
    std::atomic<bool> mTimeOffsetFinished{false};
    int64_t mCaptureStartUnixMs{0};
    int64_t mCaptureStopUnixMs{0};

    static std::string generateDatasetDirName();
    static int64_t currentUnixTimeMs();
    static std::string formatUnixTimeMs(int64_t unixTimeMs);
};
