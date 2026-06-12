#include "DatasetExporter.h"

#include <android/log.h>
#include <sys/stat.h>
#include <dirent.h>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define LOG_TAG "DatasetExporter"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

#define DATASET_EXPORT_BUFFER 1024 * 1024

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
    return content.find("\"state\": \"complete\"") != std::string::npos;
}

bool DatasetExporter::exportDataset(const std::string &datasetDir) const {
    const size_t pos = datasetDir.find_last_of('/');
    const std::string datasetName = (pos == std::string::npos) ? datasetDir : datasetDir.substr(pos + 1);

    if (datasetName.empty()) {
        return false;
    }

    if (mkdir(mExportPath.c_str(), 0777) != 0 && errno != EEXIST) {
        LOGE("mkdir export root failed: %s, err=%s", mExportPath.c_str(), strerror(errno));
        return false;
    }

    const std::string tmpDir = joinPath(mExportPath, datasetName + ".tmp");
    const std::string finalDir = joinPath(mExportPath, datasetName);
    deleteDir(tmpDir);

    if (mkdir(tmpDir.c_str(), 0777) != 0) {
        LOGE("mkdir tmp dir failed: %s, err=%s", tmpDir.c_str(), strerror(errno));
        return false;
    }

    DIR* dir = opendir(datasetDir.c_str());
    if (!dir) {
        deleteDir(tmpDir);
        return false;
    }

    bool ret = true;
    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        const std::string srcPath = joinPath(datasetDir, name);
        const std::string dstPath = joinPath(tmpDir, name);

        if (!copyFile(srcPath, dstPath)) {
            ret = false;
            break;
        }
    }

    closedir(dir);

    if (!ret) {
        deleteDir(tmpDir);
        return false;
    }

    if (rename(tmpDir.c_str(), finalDir.c_str()) != 0) {
        LOGE("rename failed in renaming: %s -> %s, err=%s", tmpDir.c_str(), finalDir.c_str(), strerror(errno));
        deleteDir(tmpDir);
        return false;
    }

    if (!deleteDir(datasetDir)) {
        LOGW("export success but delete local dir failed: %s", datasetDir.c_str());
        return false;
    }

    LOGI("export success: %s", datasetName.c_str());
    return true;
}

bool DatasetExporter::copyFile(const std::string& srcPath, const std::string& dstPath) const {
    int srcfd = open(srcPath.c_str(), O_RDONLY);
    if (srcfd < 0) {
        LOGE("open src failed: %s, err=%s", srcPath.c_str(), strerror(errno));
        return false;
    }

    int dstfd = open(dstPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dstfd < 0) {
        LOGE("open dst failed: %s, err=%s", dstPath.c_str(), strerror(errno));
        close(srcfd);
        return false;
    }

    std::vector<char> buffer(DATASET_EXPORT_BUFFER);
    bool ret = true;

    while (true) {
        ssize_t readSize = read(srcfd, buffer.data(), buffer.size());
        if (readSize == 0) {
            break;
        }
        if (readSize < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOGE("read failed: %s, err=%s", srcPath.c_str(), strerror(errno));
            ret = false;
            break;
        }

        ssize_t curSize = 0;
        while (curSize < readSize) {
            ssize_t writeSize = write(dstfd, buffer.data() + curSize, readSize - curSize);
            if (writeSize < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOGE("write failed: %s, err=%s", dstPath.c_str(), strerror(errno));
                ret = false;
                break;
            }
            curSize += writeSize;
        }

        if (!ret) {
            break;
        }
    }
    fsync(dstfd);

    close(dstfd);
    close(srcfd);
    return ret;
}

bool DatasetExporter::deleteDir(const std::string& dirPath) const {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return errno == ENOENT;
    }

    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const char* dirname = entry->d_name;
        if (strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
            continue;
        }

        const std::string filePath = joinPath(dirPath, dirname);
        if (unlink(filePath.c_str()) != 0) {
            LOGE("unlink failed: %s, err=%s", filePath.c_str(), strerror(errno));
            closedir(dir);
            return false;
        }
    }

    closedir(dir);

    if (rmdir(dirPath.c_str()) != 0) {
        LOGE("rmdir failed: %s, err=%s", dirPath.c_str(), strerror(errno));
        return false;
    }

    return true;
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
