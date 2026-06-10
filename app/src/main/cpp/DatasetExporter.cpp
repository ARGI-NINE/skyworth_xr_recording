#include "DatasetExporter.h"

#include <android/log.h>
#include <sys/stat.h>
#include <dirent.h>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <thread>

#define LOG_TAG "DatasetExporter"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

DatasetExporter::DatasetExporter() = default;

DatasetExporter::~DatasetExporter() {
    stop();
}

void DatasetExporter::init(const std::string &datasetPath) {
    mDatasetPath = datasetPath;
    LOGI("DatasetExporter initialized with datasetPath: %s", datasetPath.c_str());
}

void DatasetExporter::start(const std::string &exportPath) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mRunning.load()) {
        LOGW("DatasetExporter already running!");
        return;
    }

    mExportPath = exportPath;
    mRunning = true;
    mWorkThread = std::thread(&DatasetExporter::workThreadFunc, this);
    LOGI("Dataset exporting started: %s", mExportPath.c_str());
}

void DatasetExporter::stop() {
    {
        std::lock_guard<std::mutex > lock(mMutex);
        if (!mRunning.load()) return;
        mRunning = false;
    }

    if (mWorkThread.joinable())
        mWorkThread.join();

    LOGI("DatasetExporter stop");
}

void DatasetExporter::workThreadFunc() {
    LOGI("DatasetExporter work thread started");
    while (mRunning.load()) {
        std::string datasetPath;
        std::string exportPath;
        {
            std::lock_guard<std::mutex > lock(mMutex);
            exportPath = mExportPath;
            datasetPath = mDatasetPath;
        }
        if (datasetPath.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::vector<std::string> datasetDirs = listDatasetDirs();
        for (const std::string& datasetDir : datasetDirs) {
            if (!mRunning.load())
                break;
            if (!isComplete(datasetDir))
                continue;
            if (!exportDataset(datasetDir))
                LOGW("export failed");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    LOGI("DatasetExporter work thread quited");
}

std::vector<std::string> DatasetExporter::listDatasetDirs() const {
    std::vector<std::string> dirPaths;
    DIR* dir = opendir(mDatasetPath.c_str());
    if (!dir)
        return dirPaths;
    dirent* entry = nullptr;
    while ((entry = readdir(dir))!= nullptr) {
        const char *dirname = entry->d_name;
        if (strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0)
            continue;
        std::string dirPath = joinPath(mDatasetPath, dirname);
        dirPaths.push_back(dirPath);
    }
    closedir(dir);
    return dirPaths;
}

bool DatasetExporter::isComplete(const std::string& datasetDir) const {
    const std::string captureStatusFile = joinPath(datasetDir, "capture_status.json");

    std::string content;
    content = readJsonToString(captureStatusFile);
    return content.find("\"status\": \"complete\"");
}

bool DatasetExporter::exportDataset(const std::string &datasetDir) const {

}

std::string DatasetExporter::readJsonToString(const std::string& filepath) const {
    std::ifstream in(filepath);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string DatasetExporter::joinPath(const std::string& dir1, const std::string& dir2) const {
    if (dir1.empty())
        return dir2;
    if (dir1.back() == '/')
        return dir1 + dir2;
    return dir1 + '/' + dir2;
}


