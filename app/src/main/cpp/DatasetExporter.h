#pragma once

#include <string>
#include <mutex>
#include <atomic>

class DatasetExporter {
public:
    DatasetExporter();
    ~DatasetExporter();

    void init(const std::string& datasetPath);

    void start(const std::string& exportPath);

    void stop();

    bool isRunning() const { return mRunning.load(); }

private:
    std::mutex mMutex;
    std::string mBasePath;
    std::string mExportPath;

    std::atomic<bool> mRunning;
};

