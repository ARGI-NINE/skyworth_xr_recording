#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <map>
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

    // Get BOOTTIME→REALTIME offset captured at recording start
    int64_t getTimeOffset() const { return mBoottimeToRealtimeOffsetNs; }

    // Save head pose from render thread (async, non-blocking)
    void saveHeadPose(int64_t boottimeNs, const XrPosef& pose);

private:
    void poseWriterThreadFunc();

    std::string mBasePath;
    std::string mDatasetDir;
    std::atomic<bool> mRecording{false};
    std::mutex mMutex;

    // BOOTTIME→REALTIME offset captured once at recording start
    int64_t mBoottimeToRealtimeOffsetNs{0};

    // IMU collector
    ImuPoseCollector mImuCollector;

    // Audio encoder
    AudioEncoder mAudioEncoder;

    // Head pose async writer with reorder buffer.
    // Entries are stored in a sorted map keyed by timestamp so that the writer
    // thread always emits rows in monotonically increasing order, even when
    // camera frames arrive out-of-order in the callback.
    static constexpr int64_t REORDER_WINDOW_NS = 100000000LL;  // 100 ms
    struct PoseEntry {
        int64_t timestamp;
        float pos[3];
        float quat[4];
    };
    std::map<int64_t, PoseEntry> mPoseMap;
    int64_t mMaxSeenPoseTimestamp{0};
    std::mutex mPoseMutex;
    std::condition_variable mPoseCV;
    std::thread mPoseWriterThread;
    std::ofstream mPoseFile;
    std::atomic<bool> mPoseWriterRunning{false};
    std::atomic<bool> mPoseWriterFinished{false};
    std::atomic<uint64_t> mPoseCount{0};

    int64_t mCaptureStartUnixMs{0};
    int64_t mCaptureStopUnixMs{0};

    static std::string generateDatasetDirName();
    static int64_t currentUnixTimeMs();
    static std::string formatUnixTimeMs(int64_t unixTimeMs);
};
