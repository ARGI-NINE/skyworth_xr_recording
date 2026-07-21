#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <fstream>

struct ControllerPoseRecord {
    uint32_t frameNumber;
    int64_t timestamp;

    bool leftActive;
    float leftPos[3];
    float leftQuat[4];

    bool rightActive;
    float rightPos[3];
    float rightQuat[4];
};

class ControllerPoseSaver {
public:
    ControllerPoseSaver();
    ~ControllerPoseSaver();

    void Init(const std::string& savePath);
    void Shutdown();

    // Set BOOTTIME→REALTIME offset for timestamp conversion (called once per recording session)
    void SetTimeOffset(int64_t offsetNs) { m_timeOffsetNs = offsetNs; }

    bool StartSession(const std::string& csvPath);
    void StopSession();
    bool IsSessionActive() const { return m_sessionActive.load(); }
    bool IsFinished() const { return m_finished.load(); }

    bool SaveFrame(const ControllerPoseRecord& record);
    void Pause();
    void Resume();

private:
    void WriterThread();
    std::string GenerateHeader();
    std::string RecordToCsv(const ControllerPoseRecord& rec);

    std::string m_savePath;

    // BOOTTIME→REALTIME offset for timestamp conversion
    int64_t m_timeOffsetNs{0};
    std::queue<ControllerPoseRecord> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;

    std::thread m_writerThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_sessionActive{false};
    std::atomic<bool> m_finished{true};
    bool m_isSaving{false}; // guarded by m_queueMutex
    std::atomic<uint32_t> m_savedCount{0};

    std::ofstream m_csvFile;
    std::mutex m_csvMutex;
};
