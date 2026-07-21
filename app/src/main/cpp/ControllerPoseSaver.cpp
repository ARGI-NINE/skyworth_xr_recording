#include "ControllerPoseSaver.h"
#include "NativeLogger.h"
#include <android/log.h>
#include <sys/stat.h>
#include <sstream>
#include <iomanip>

#define LOG_TAG "ControllerPoseSaver"
#define LOGI(...) NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGE(...) NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

ControllerPoseSaver::ControllerPoseSaver() = default;

ControllerPoseSaver::~ControllerPoseSaver() {
    Shutdown();
}

void ControllerPoseSaver::Init(const std::string& savePath) {
    if (m_running) return;
    m_savePath = savePath;
    m_running = true;
    m_paused = false;
    m_savedCount = 0;
    m_writerThread = std::thread(&ControllerPoseSaver::WriterThread, this);
    LOGI("ControllerPoseSaver initialized: %s", savePath.c_str());
}

void ControllerPoseSaver::Shutdown() {
    if (!m_running) return;
    m_running = false;
    m_queueCv.notify_all();
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_csvMutex);
        if (m_csvFile.is_open()) {
            m_csvFile.flush();
            m_csvFile.close();
        }
    }
    LOGI("ControllerPoseSaver shutdown. Saved %u frames", m_savedCount.load());
}

bool ControllerPoseSaver::StartSession(const std::string& csvPath) {
    std::lock_guard<std::mutex> lock(m_csvMutex);
    if (m_csvFile.is_open()) {
        m_csvFile.flush();
        m_csvFile.close();
    }
    m_csvFile.open(csvPath, std::ios::out | std::ios::trunc);
    if (!m_csvFile.is_open()) {
        LOGE("Failed to open CSV: %s", csvPath.c_str());
        return false;
    }
    m_csvFile << GenerateHeader();
    m_csvFile.flush();
    m_finished = false;
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        m_sessionActive = true;
    }
    LOGI("ControllerPoseSaver session started: %s", csvPath.c_str());
    return true;
}

void ControllerPoseSaver::StopSession() {
    m_paused = false;
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        // Close admission while holding the queue lock, then drain every frame
        // that was accepted for this session before closing the CSV.
        m_sessionActive = false;
        m_queueCv.notify_all();
        m_queueCv.wait(lock, [this] {
            return m_queue.empty() && !m_isSaving;
        });
    }
    std::lock_guard<std::mutex> lock(m_csvMutex);
    if (m_csvFile.is_open()) {
        m_csvFile.flush();
        m_csvFile.close();
    }
    m_finished = true;
    LOGI("ControllerPoseSaver session stopped");
}

bool ControllerPoseSaver::SaveFrame(const ControllerPoseRecord& record) {
    if (!m_running || m_paused) return false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (!m_sessionActive.load()) return false;
        m_queue.push(record);
    }
    m_queueCv.notify_one();
    return true;
}

void ControllerPoseSaver::Pause() {
    m_paused = true;
}

void ControllerPoseSaver::Resume() {
    m_paused = false;
    m_queueCv.notify_all();
}

void ControllerPoseSaver::WriterThread() {
    while (m_running) {
        ControllerPoseRecord rec;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] {
                return !m_queue.empty() || !m_running;
            });
            if (!m_running) break;
            if (m_paused || m_queue.empty()) continue;
            m_isSaving = true;
            rec = m_queue.front();
            m_queue.pop();
        }

        std::string line = RecordToCsv(rec);
        {
            std::lock_guard<std::mutex> lock(m_csvMutex);
            if (m_csvFile.is_open()) {
                m_csvFile << line;
                m_csvFile.flush();
            }
        }
        m_savedCount++;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_isSaving = false;
        }
        m_queueCv.notify_all();
    }
}

std::string ControllerPoseSaver::GenerateHeader() {
    return "frame_number,timestamp_ns,left_active,"
           "left_px,left_py,left_pz,left_qx,left_qy,left_qz,left_qw,"
           "right_active,"
           "right_px,right_py,right_pz,right_qx,right_qy,right_qz,right_qw\n";
}

std::string ControllerPoseSaver::RecordToCsv(const ControllerPoseRecord& rec) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << rec.frameNumber << ","
        << (rec.timestamp + m_timeOffsetNs) << ","
        << (rec.leftActive ? 1 : 0) << ","
        << rec.leftPos[0] << "," << rec.leftPos[1] << "," << rec.leftPos[2] << ","
        << rec.leftQuat[0] << "," << rec.leftQuat[1] << ","
        << rec.leftQuat[2] << "," << rec.leftQuat[3] << ","
        << (rec.rightActive ? 1 : 0) << ","
        << rec.rightPos[0] << "," << rec.rightPos[1] << "," << rec.rightPos[2] << ","
        << rec.rightQuat[0] << "," << rec.rightQuat[1] << ","
        << rec.rightQuat[2] << "," << rec.rightQuat[3] << "\n";
    return oss.str();
}
