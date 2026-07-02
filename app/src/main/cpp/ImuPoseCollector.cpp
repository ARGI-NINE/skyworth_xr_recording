#include "ImuPoseCollector.h"
#include "NativeLogger.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "ImuPoseCollector"
#define LOGI(...) NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGW(...) NATIVE_LOGW(LOG_TAG, __VA_ARGS__)
#define LOGE(...) NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

ImuPoseCollector::ImuPoseCollector() = default;

ImuPoseCollector::~ImuPoseCollector() {
    stop();
}

bool ImuPoseCollector::start(const std::string& accelCsvPath, const std::string& gyroCsvPath) {
    if (mRunning.load()) {
        LOGW("Already running");
        return false;
    }

    mAccelCount = 0;
    mGyroCount = 0;
    mFinished = false;

    mAccelFile.open(accelCsvPath, std::ios::out | std::ios::trunc);
    if (!mAccelFile.is_open()) {
        LOGE("Failed to open accel CSV: %s", accelCsvPath.c_str());
        return false;
    }
    mAccelFile << "timestamp_ns,x,y,z\n";
    mAccelFile.flush();

    mGyroFile.open(gyroCsvPath, std::ios::out | std::ios::trunc);
    if (!mGyroFile.is_open()) {
        LOGE("Failed to open gyro CSV: %s", gyroCsvPath.c_str());
        mAccelFile.close();
        return false;
    }
    mGyroFile << "timestamp_ns,x,y,z\n";
    mGyroFile.flush();

    mSensorManager = ASensorManager_getInstanceForPackage("com.ssnwt.helloxr");
    if (!mSensorManager) {
        LOGE("Failed to get ASensorManager");
        mAccelFile.close();
        mGyroFile.close();
        return false;
    }

    mWriterRunning = true;
    mWriterThread = std::thread(&ImuPoseCollector::writerThreadFunc, this);

    mRunning = true;
    mSensorThread = std::thread(&ImuPoseCollector::sensorThreadFunc, this);

    LOGI("ImuPoseCollector started: %s, %s", accelCsvPath.c_str(), gyroCsvPath.c_str());
    return true;
}

void ImuPoseCollector::stop() {
    if (!mRunning.load()) return;

    mRunning = false;
    if (mLooper) ALooper_wake(mLooper);
    if (mSensorThread.joinable()) mSensorThread.join();

    mWriterRunning = false;
    mQueueCV.notify_all();
    if (mWriterThread.joinable()) mWriterThread.join();

    // Flush remaining
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        while (!mWriteQueue.empty()) {
            auto& e = mWriteQueue.front();
            int64_t utcNs = e.timestamp + mTimeOffsetNs;
            if (e.isAccel) {
                mAccelFile << utcNs << "," << e.x << "," << e.y << "," << e.z << "\n";
            } else {
                mGyroFile << utcNs << "," << e.x << "," << e.y << "," << e.z << "\n";
            }
            mWriteQueue.pop();
        }
    }

    if (mAccelFile.is_open()) { mAccelFile.flush(); mAccelFile.close(); }
    if (mGyroFile.is_open()) { mGyroFile.flush(); mGyroFile.close(); }

    mFinished = true;

    LOGI("ImuPoseCollector stopped. Accel: %lu, Gyro: %lu",
         (unsigned long)mAccelCount.load(), (unsigned long)mGyroCount.load());
}

void ImuPoseCollector::sensorThreadFunc() {
    mLooper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    mEventQueue = ASensorManager_createEventQueue(mSensorManager, mLooper, 100, nullptr, nullptr);
    if (!mEventQueue) {
        LOGE("Failed to create sensor event queue");
        mRunning = false;
        return;
    }

    const ASensor* accelSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_ACCELEROMETER);
    if (accelSensor) {
        int ret = ASensorEventQueue_enableSensor(mEventQueue, accelSensor);
        if (ret < 0) {
            LOGE("Failed to enable accelerometer: %d", ret);
        } else {
            ASensorEventQueue_setEventRate(mEventQueue, accelSensor, 0);
            LOGI("Accelerometer enabled at FASTEST rate");
        }
    } else {
        LOGW("No accelerometer found");
    }

    const ASensor* gyroSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_GYROSCOPE);
    if (gyroSensor) {
        int ret = ASensorEventQueue_enableSensor(mEventQueue, gyroSensor);
        if (ret < 0) {
            LOGE("Failed to enable gyroscope: %d", ret);
        } else {
            ASensorEventQueue_setEventRate(mEventQueue, gyroSensor, 0);
            LOGI("Gyroscope enabled at FASTEST rate");
        }
    } else {
        LOGW("No gyroscope found");
    }

    while (mRunning.load()) {
        int ident = ALooper_pollOnce(1, nullptr, nullptr, nullptr);
        if (ident == ALOOPER_POLL_TIMEOUT || ident == ALOOPER_POLL_WAKE) continue;

        ASensorEvent event;
        while (ASensorEventQueue_getEvents(mEventQueue, &event, 1) > 0) {
            ImuEvent ie{};
            ie.timestamp = event.timestamp;

            if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
                ie.x = event.acceleration.x;
                ie.y = event.acceleration.y;
                ie.z = event.acceleration.z;
                ie.isAccel = true;
            } else if (event.type == ASENSOR_TYPE_GYROSCOPE) {
                ie.x = event.data[0];
                ie.y = event.data[1];
                ie.z = event.data[2];
                ie.isAccel = false;
            } else {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mQueueMutex);
                mWriteQueue.push(ie);
            }
            mQueueCV.notify_one();
        }
    }

    if (accelSensor) ASensorEventQueue_disableSensor(mEventQueue, accelSensor);
    if (gyroSensor) ASensorEventQueue_disableSensor(mEventQueue, gyroSensor);
    if (mEventQueue && mSensorManager) {
        ASensorManager_destroyEventQueue(mSensorManager, mEventQueue);
        mEventQueue = nullptr;
    }
    mLooper = nullptr;
    LOGI("Sensor thread exited");
}

void ImuPoseCollector::writerThreadFunc() {
    LOGI("Writer thread started");

    while (mWriterRunning.load() || !mWriteQueue.empty()) {
        ImuEvent event;
        bool hasEvent = false;

        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mQueueCV.wait_for(lock, std::chrono::milliseconds(1), [this] {
                return !mWriteQueue.empty() || !mWriterRunning.load();
            });
            if (!mWriteQueue.empty()) {
                event = mWriteQueue.front();
                mWriteQueue.pop();
                hasEvent = true;
            }
        }

        if (hasEvent) {
            int64_t utcNs = event.timestamp + mTimeOffsetNs;
            if (event.isAccel) {
                mAccelFile << utcNs << "," << event.x << "," << event.y << "," << event.z << "\n";
                mAccelCount++;
                if (mAccelCount % 200 == 0) mAccelFile.flush();
            } else {
                mGyroFile << utcNs << "," << event.x << "," << event.y << "," << event.z << "\n";
                mGyroCount++;
                if (mGyroCount % 200 == 0) mGyroFile.flush();
            }
        }
    }

    if (mAccelFile.is_open()) mAccelFile.flush();
    if (mGyroFile.is_open()) mGyroFile.flush();
    LOGI("Writer thread exited");
}
