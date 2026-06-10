#include "DatasetExporter.h"

#include <android/log.h>
#include <sys/stat.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#define LOG_TAG "DatasetExporter"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

DatasetExporter::DatasetExporter() = default;

DatasetExporter::~DatasetExporter() {
    stop();
}

void DatasetExporter::init(const std::string &datasetPath) {
    mBasePath = datasetPath;
}

void DatasetExporter::start(const std::string &exportPath) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mRunning.load()) {
        LOGW("DatasetExporter already running!");
        return;
    }

    mExportPath = exportPath;
}

void DatasetExporter::stop() {

}