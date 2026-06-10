#pragma once

#include <string>
#include <mutex>
#include <atomic>
#include <thread>

class DatasetExporter {
public:
    DatasetExporter();
    ~DatasetExporter();

    void init(const std::string& datasetPath);

    void start(const std::string& exportPath);

    void stop();

    bool isRunning() const { return mRunning.load(); }

private:
    void workThreadFunc();

    std::mutex mMutex;
    std::string mBasePath;
    std::string mExportPath;
    std::thread mWorkThread;
    std::atomic<bool> mRunning;
};

