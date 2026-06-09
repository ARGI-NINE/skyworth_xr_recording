#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>

struct SaveRequest {
    std::string filePath;
    std::vector<uint8_t> rgbaData;
    int width;
    int height;
};

class ImageSaver {
public:
    static ImageSaver& Instance();

    void start();
    void shutdown();

    // Save RGBA data as PNG (async, returns immediately)
    bool saveImage(const std::string& filePath,
                   const std::vector<uint8_t>& rgbaData,
                   int width, int height);

    // Synchronous version
    bool saveImageSync(const std::string& filePath,
                       const std::vector<uint8_t>& rgbaData,
                       int width, int height);

    bool isRunning() const { return m_running.load(); }

private:
    ImageSaver() = default;
    ~ImageSaver();

    void workerThread();
    bool ensureDirectory(const std::string& filePath);

    std::queue<SaveRequest> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};
