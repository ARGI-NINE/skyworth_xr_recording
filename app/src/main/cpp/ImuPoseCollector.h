#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>

#include <android/sensor.h>
#include <android/looper.h>

class ImuPoseCollector {
public:
    ImuPoseCollector();
    ~ImuPoseCollector();

    // Start collecting IMU data to separate accel.csv and gyro.csv files.
    bool start(const std::string& accelCsvPath, const std::string& gyroCsvPath);

    void stop();

    bool isRunning() const { return mRunning.load(); }
    bool isFinished() const { return mFinished.load(); }

    // Set BOOTTIME→REALTIME offset for timestamp conversion (called once per recording session)
    void setTimeOffset(int64_t offsetNs) { mTimeOffsetNs = offsetNs; }

private:
    void sensorThreadFunc();
    void writerThreadFunc();

    // Write queue entry
    struct ImuEvent {
        int64_t timestamp;
        float x, y, z;
        bool isAccel;  // true=accelerometer, false=gyroscope
    };

    // Sensor state
    ASensorManager* mSensorManager{nullptr};
    ASensorEventQueue* mEventQueue{nullptr};
    ALooper* mLooper{nullptr};
    std::thread mSensorThread;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mFinished{false};

    // Write queue
    std::queue<ImuEvent> mWriteQueue;
    std::mutex mQueueMutex;
    std::condition_variable mQueueCV;
    std::thread mWriterThread;
    std::atomic<bool> mWriterRunning{false};

    // CSV output
    std::ofstream mAccelFile;
    std::ofstream mGyroFile;
    std::atomic<uint64_t> mAccelCount{0};
    std::atomic<uint64_t> mGyroCount{0};

    // BOOTTIME→REALTIME offset for timestamp conversion
    int64_t mTimeOffsetNs{0};
};
