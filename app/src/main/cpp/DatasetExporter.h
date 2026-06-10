#pragma once

#include <string>
#include <vector>
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

    std::vector<std::string> listDatasetDirs() const;
    bool isComplete(const std::string& datasetDir) const;
    bool exportDataset(const std::string& datasetDir) const;
    bool copyFile(const std::string& srcPath, const std::string& dstPath) const;
    bool deleteDir(const std::string& dirPath) const;
    std::string readJsonToString(const std::string& filepath) const;
    std::string joinPath(const std::string& dir1, const std::string& dir2) const;

    std::mutex mMutex;
    std::string mDatasetPath;
    std::string mExportPath;
    std::thread mWorkThread;
    std::atomic<bool> mRunning;
};

