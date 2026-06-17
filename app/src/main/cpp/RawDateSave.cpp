/****************************************************************
 * Copyright (c) 2026
 * All Rights Reserved.
 ****************************************************************/

#include "RawDateSave.h"
#include "NativeLogger.h"
#include "xr_logger.h"
#include <android/log.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iomanip>
#include <cstring>

#define LOG_TAG "RawDateSave"
#define LOGI(...) NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGW(...) NATIVE_LOGW(LOG_TAG, __VA_ARGS__)
#define LOGE(...) NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

// 手势关节名称映射 (XR_HAND_JOINT_26个关节)
static const char* JOINT_NAMES[26] = {
    "PALM",             // 0
    "WRIST",            // 1
    "THUMB_METACARPAL",  // 2
    "THUMB_PROXIMAL",   // 3
    "THUMB_DISTAL",     // 4
    "THUMB_TIP",        // 5
    "INDEX_METACARPAL",  // 6
    "INDEX_PROXIMAL",   // 7
    "INDEX_INTERMEDIATE", // 8
    "INDEX_DISTAL",     // 9
    "INDEX_TIP",        // 10
    "MIDDLE_METACARPAL", // 11
    "MIDDLE_PROXIMAL",  // 12
    "MIDDLE_INTERMEDIATE", // 13
    "MIDDLE_DISTAL",    // 14
    "MIDDLE_TIP",       // 15
    "RING_METACARPAL",  // 16
    "RING_PROXIMAL",   // 17
    "RING_INTERMEDIATE", // 18
    "RING_DISTAL",      // 19
    "RING_TIP",         // 20
    "LITTLE_METACARPAL", // 21
    "LITTLE_PROXIMAL",  // 22
    "LITTLE_INTERMEDIATE", // 23
    "LITTLE_DISTAL",     // 24
    "LITTLE_TIP"        // 25
};

RawDateSave::RawDateSave()
    : m_isRunning(false)
    , m_isPaused(false)
    , m_isSaving(false)
    , m_savedFrameCount(0)
    , m_frameCounter(0)
{
}

RawDateSave::~RawDateSave() {
    Shutdown();
}

bool RawDateSave::Init(const std::string& savePath) {
    m_savePath = savePath;

    // 确保目录存在
    if (!EnsureDirectoryExists(m_savePath)) {
        LOGE("Failed to create directory: %s", m_savePath.c_str());
        return false;
    }

    // CSV file will be created by StartNewSession() when recording starts

    // 启动保存线程
    m_isRunning = true;
    m_isPaused = false;
    m_frameCounter = 0;
    m_savedFrameCount = 0;

    m_saveThread = std::thread(&RawDateSave::SaveWorkerThread, this);

    LOGI("RawDateSave initialized with path: %s", m_savePath.c_str());
    return true;
}

bool RawDateSave::StartNewSession(const std::string& csvPath) {
    std::lock_guard<std::mutex> lock(m_csvMutex);

    // Close existing file if open
    if (m_csvFile.is_open()) {
        m_csvFile.flush();
        m_csvFile.close();
    }

    m_csvFile.open(csvPath, std::ios::out | std::ios::trunc);
    if (!m_csvFile.is_open()) {
        LOGE("Failed to open session CSV: %s", csvPath.c_str());
        return false;
    }

    m_csvFile << GenerateCsvHeader();
    m_csvFile.flush();
    m_finished = false;
    m_sessionActive = true;

    LOGI("RawDateSave session started: %s", csvPath.c_str());
    return true;
}

void RawDateSave::StopSession() {
    m_isPaused = false;
    m_queueCondition.notify_all();

    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_queueCondition.wait(lock, [this] {
            return m_frameQueue.empty() && !m_isSaving.load();
        });
    }

    m_sessionActive = false;

    std::lock_guard<std::mutex> lock(m_csvMutex);
    if (m_csvFile.is_open()) {
        m_csvFile.flush();
        m_csvFile.close();
    }
    m_finished = true;

    LOGI("RawDateSave session stopped");
}

void RawDateSave::Shutdown() {
    if (!m_isRunning) {
        return;
    }

    // 停止保存线程
    m_isRunning = false;
    m_isPaused = false;

    // 唤醒线程
    m_queueCondition.notify_all();

    // 等待线程结束
    if (m_saveThread.joinable()) {
        m_saveThread.join();
    }

    // 关闭CSV文件
    {
        std::lock_guard<std::mutex> lock(m_csvMutex);
        if (m_csvFile.is_open()) {
            m_csvFile.flush();
            m_csvFile.close();
        }
    }

    // 清空队列
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::queue<FrameData> emptyQueue;
        m_frameQueue.swap(emptyQueue);
    }

    LOGI("RawDateSave shutdown. Total frames saved: %u", m_savedFrameCount.load());
}

bool RawDateSave::SaveFrame(const FrameData& frameData) {
    if (!m_isRunning || m_isPaused) {
        return false;
    }
    // Only save if a recording session is active
    if (!m_sessionActive.load()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_frameQueue.push(frameData);
    }

    m_queueCondition.notify_one();
    return true;
}

void RawDateSave::Pause() {
    m_isPaused = true;
    LOGI("RawDateSave paused");
}

void RawDateSave::Resume() {
    m_isPaused = false;
    m_queueCondition.notify_all();
    LOGI("RawDateSave resumed");
}

size_t RawDateSave::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_frameQueue.size();
}

void RawDateSave::SaveWorkerThread() {
    LOGI("SaveWorkerThread started");

    while (m_isRunning) {
        FrameData frameData;

        // 从队列中获取数据
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait(lock, [this] {
                return !m_frameQueue.empty() || !m_isRunning;
            });

            if (!m_isRunning) {
                break;
            }

            if (m_isPaused) {
                continue;
            }

            if (m_frameQueue.empty()) {
                continue;
            }

            m_isSaving = true;
            frameData = m_frameQueue.front();
            m_frameQueue.pop();
        }

        if (!m_sessionActive.load()) {
            m_isSaving = false;
            m_queueCondition.notify_all();
            continue;
        }

        std::string csvContent = FrameToCsv(frameData);

        {
            std::lock_guard<std::mutex> lock(m_csvMutex);
            if (m_csvFile.is_open()) {
                m_csvFile << csvContent;
                m_csvFile.flush();
            }
        }

        m_savedFrameCount++;
        m_isSaving = false;
        m_queueCondition.notify_all();
    }

    LOGI("SaveWorkerThread ended");
}

std::string RawDateSave::GenerateCsvHeader() {
    std::ostringstream oss;

    oss << "frame_number,timestamp,left_active,right_active";

    // 左手26个关节: id, name,radius, pos_x, pos_y, pos_z
    for (uint32_t i = 0; i < 26; ++i) {
        oss << ",left_joint" << i << "_id,left_joint" << i << "_name,left_joint" << i << "_radius,left_joint" << i << "_pos_x,left_joint" << i << "_pos_y,left_joint" << i << "_pos_z,left_joint"
                << i << "_orientation_x,left_joint" << i << "_orientation_y,left_joint" << i << "_orientation_z,left_joint" << i << "_orientation_w";
    }

    // 右手26个关节: id, name,radius, pos_x, pos_y, pos_z
    for (uint32_t i = 0; i < 26; ++i) {
        oss << ",right_joint" << i << "_id,right_joint" << i << "_name,right_joint" << i << "_radius,right_joint" << i << "_pos_x,right_joint" << i << "_pos_y,right_joint" << i << "_pos_z,right_joint"
                << i << "_orientation_x,right_joint" << i << "_orientation_y,right_joint" << i << "_orientation_z,right_joint" << i << "_orientation_w";
    }

    oss << "\n";
    return oss.str();
}

std::string RawDateSave::FrameToCsv(const FrameData& frameData) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    // 基础信息
    oss << frameData.frameNumber << ","
        << frameData.timestamp << ","
        << (frameData.hasLeftHand && frameData.leftHand.isActive ? 1 : 0) << ","
        << (frameData.hasRightHand && frameData.rightHand.isActive ? 1 : 0);

    // 左手26个关节数据: id, name,radius, pos_x, pos_y, pos_z
    for (uint32_t i = 0; i < 26; ++i) {
        if (frameData.hasLeftHand && frameData.leftHand.isActive) {
            const HandJointPosition& joint = frameData.leftHand.joints[i];
            oss << "," << i
                << "," << JOINT_NAMES[i]
                << "," << joint.radius
                << "," << joint.position[0]
                << "," << joint.position[1]
                << "," << joint.position[2]
                << "," << joint.orientation[0]
                << "," << joint.orientation[1]
                << "," << joint.orientation[2]
                << "," << joint.orientation[3];
        } else {
            // 手部不活跃时填充空值: id, name, radius, pos_x, pos_y, pos_z, orientation_x, orientation_y, orientation_z, orientation_w
            oss << ",-1,\"\",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0";
        }
    }

    // 右手26个关节数据: id, name, radius, pos_x, pos_y, pos_z
    for (uint32_t i = 0; i < 26; ++i) {
        if (frameData.hasRightHand && frameData.rightHand.isActive) {
            const HandJointPosition& joint = frameData.rightHand.joints[i];
            oss << "," << i
                << "," << JOINT_NAMES[i]
                << "," << joint.radius
                << "," << joint.position[0]
                << "," << joint.position[1]
                << "," << joint.position[2]
                << "," << joint.orientation[0]
                << "," << joint.orientation[1]
                << "," << joint.orientation[2]
                << "," << joint.orientation[3];
        } else {
            // 手部不活跃时填充空值: id, name, radius, pos_x, pos_y, pos_z, orientation_x, orientation_y, orientation_z, orientation_w
            oss << ",-1,\"\",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0";
        }
    }

    oss << "\n";
    return oss.str();
}

bool RawDateSave::EnsureDirectoryExists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        return false;
    }

    // 创建目录
    if (mkdir(path.c_str(), 0777) != 0) {
        return false;
    }

    return true;
}
