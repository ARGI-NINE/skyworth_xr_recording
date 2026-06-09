#include "ImageSaver.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <android/log.h>
#include <sys/stat.h>
#include <cstring>

#define LOG_TAG "ImageSaver"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

ImageSaver& ImageSaver::Instance() {
    static ImageSaver instance;
    return instance;
}

ImageSaver::~ImageSaver() {
    shutdown();
}

void ImageSaver::start() {
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&ImageSaver::workerThread, this);
    LOGI("ImageSaver started");
}

void ImageSaver::shutdown() {
    if (!m_running.exchange(false)) return;
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    LOGI("ImageSaver shutdown");
}

void ImageSaver::workerThread() {
    while (m_running.load()) {
        SaveRequest req;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running.load(); });
            if (!m_running.load() && m_queue.empty()) break;
            if (m_queue.empty()) continue;
            req = std::move(m_queue.front());
            m_queue.pop();
        }

        ensureDirectory(req.filePath);

        int result = stbi_write_png(req.filePath.c_str(), req.width, req.height,
                                     4, req.rgbaData.data(), req.width * 4);
        if (result) {
            LOGI("Saved PNG: %s (%dx%d)", req.filePath.c_str(), req.width, req.height);
        } else {
            LOGE("Failed to save PNG: %s", req.filePath.c_str());
        }
    }
    // Drain remaining queue
    while (!m_queue.empty()) {
        SaveRequest req = std::move(m_queue.front());
        m_queue.pop();
        ensureDirectory(req.filePath);
        stbi_write_png(req.filePath.c_str(), req.width, req.height,
                       4, req.rgbaData.data(), req.width * 4);
    }
}

bool ImageSaver::saveImage(const std::string& filePath,
                            const std::vector<uint8_t>& rgbaData,
                            int width, int height) {
    if (!m_running.load()) {
        LOGE("ImageSaver not running, cannot save %s", filePath.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SaveRequest req;
        req.filePath = filePath;
        req.rgbaData = rgbaData;
        req.width = width;
        req.height = height;
        m_queue.push(std::move(req));
    }
    m_cv.notify_one();
    return true;
}

bool ImageSaver::saveImageSync(const std::string& filePath,
                                const std::vector<uint8_t>& rgbaData,
                                int width, int height) {
    ensureDirectory(filePath);
    int result = stbi_write_png(filePath.c_str(), width, height,
                                 4, rgbaData.data(), width * 4);
    if (result) {
        LOGI("Saved PNG (sync): %s (%dx%d)", filePath.c_str(), width, height);
    } else {
        LOGE("Failed to save PNG (sync): %s", filePath.c_str());
    }
    return result != 0;
}

bool ImageSaver::ensureDirectory(const std::string& filePath) {
    size_t pos = filePath.find_last_of('/');
    if (pos == std::string::npos) return true;
    std::string dir = filePath.substr(0, pos);

    for (size_t i = 1; i < dir.size(); i++) {
        if (dir[i] == '/') {
            std::string sub = dir.substr(0, i);
            mkdir(sub.c_str(), 0777);
        }
    }
    mkdir(dir.c_str(), 0777);
    return true;
}
