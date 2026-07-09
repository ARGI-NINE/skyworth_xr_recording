#include "NativeLogger.h"
#include "ble_time_sync/BleTimeSyncService.h"

#include <android/log.h>
#include <jni.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace {

constexpr char kLogTag[] = "BleTimeSync";

class TimeSyncHandleTable {
public:
    jlong Create() {
        std::lock_guard<std::mutex> lock(mutex_);
        const jlong handle = nextHandle_++;
        sessions_.emplace(handle, std::make_shared<ble_time_sync::BleTimeSyncService>());
        return handle;
    }

    void Destroy(jlong handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(handle);
    }

    std::shared_ptr<ble_time_sync::BleTimeSyncService> Find(jlong handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(handle);
        if (it == sessions_.end()) {
            return {};
        }
        return it->second;
    }

private:
    std::mutex mutex_;
    jlong nextHandle_ = 1;
    std::unordered_map<jlong, std::shared_ptr<ble_time_sync::BleTimeSyncService>> sessions_;
};

TimeSyncHandleTable& HandleTable() {
    static TimeSyncHandleTable table;
    return table;
}

class LegacySessionBridge {
public:
    jlong EnsureHandle() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle_ == 0) {
            handle_ = HandleTable().Create();
            NATIVE_LOGI(kLogTag,
                        "event=create_legacy_handle handle=%lld",
                        static_cast<long long>(handle_));
        }
        return handle_;
    }

    void DestroyHandle() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle_ == 0) {
            return;
        }
        const jlong handle = handle_;
        handle_ = 0;
        HandleTable().Destroy(handle);
        NATIVE_LOGI(kLogTag,
                    "event=destroy_legacy_handle handle=%lld",
                    static_cast<long long>(handle));
    }

private:
    std::mutex mutex_;
    jlong handle_ = 0;
};

LegacySessionBridge& LegacyBridge() {
    static LegacySessionBridge bridge;
    return bridge;
}

jstring ToJString(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

std::string JStringToUtf8(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return std::string();
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return std::string();
    }

    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

std::shared_ptr<ble_time_sync::BleTimeSyncService> RequireSession(jlong handle) {
    std::shared_ptr<ble_time_sync::BleTimeSyncService> session = HandleTable().Find(handle);
    if (session == nullptr) {
        NATIVE_LOGW(kLogTag, "event=invalid_handle handle=%lld", static_cast<long long>(handle));
    }
    return session;
}

std::string OnBleReadyForHandle(jlong handle, int64_t nowBootTimeNs) {
    std::shared_ptr<ble_time_sync::BleTimeSyncService> session = RequireSession(handle);
    if (session == nullptr) {
        return std::string();
    }
    return session->OnBleReady(nowBootTimeNs);
}

std::string OnBleCommandForHandle(jlong handle,
                                  const std::string& commandJson,
                                  int64_t recvBootTimeNs) {
    std::shared_ptr<ble_time_sync::BleTimeSyncService> session = RequireSession(handle);
    if (session == nullptr) {
        return std::string();
    }
    return session->OnControlCommand(commandJson, recvBootTimeNs);
}

void OnBleDisconnectedForHandle(jlong handle) {
    std::shared_ptr<ble_time_sync::BleTimeSyncService> session = RequireSession(handle);
    if (session == nullptr) {
        return;
    }
    session->OnDisconnected();
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeInitBleService(
        JNIEnv* env,
        jobject /*thiz*/,
        jstring filesDir) {
    const char* filesDirChars = filesDir != nullptr ? env->GetStringUTFChars(filesDir, nullptr) : nullptr;
    if (filesDirChars == nullptr || filesDirChars[0] == '\0') {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "nativeInitBleService skipped: filesDir is empty");
    } else {
        NativeLoggerInit(filesDirChars);
        NATIVE_LOGI(kLogTag, "BleService JNI logger initialized: %s", filesDirChars);
    }
    if (filesDirChars != nullptr) {
        env->ReleaseStringUTFChars(filesDir, filesDirChars);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeCreateTimeSyncHandle(
        JNIEnv* /*env*/,
        jobject /*thiz*/) {
    const jlong handle = HandleTable().Create();
    NATIVE_LOGI(kLogTag, "event=create_handle handle=%lld", static_cast<long long>(handle));
    return handle;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeDestroyTimeSyncHandle(
        JNIEnv* /*env*/,
        jobject /*thiz*/,
        jlong handle) {
    HandleTable().Destroy(handle);
    NATIVE_LOGI(kLogTag, "event=destroy_handle handle=%lld", static_cast<long long>(handle));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnBleReady__JJ(
        JNIEnv* env,
        jobject /*thiz*/,
        jlong handle,
        jlong nowBootTimeNs) {
    return ToJString(env, OnBleReadyForHandle(handle, static_cast<int64_t>(nowBootTimeNs)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnBleCommand__JLjava_lang_String_2J(
        JNIEnv* env,
        jobject /*thiz*/,
        jlong handle,
        jstring commandJson,
        jlong recvBootTimeNs) {
    return ToJString(
            env,
            OnBleCommandForHandle(
                    handle,
                    JStringToUtf8(env, commandJson),
                    static_cast<int64_t>(recvBootTimeNs)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnControlCommand(
        JNIEnv* env,
        jobject /*thiz*/,
        jlong handle,
        jstring commandJson,
        jlong recvBootTimeNs) {
    return ToJString(
            env,
            OnBleCommandForHandle(
                    handle,
                    JStringToUtf8(env, commandJson),
                    static_cast<int64_t>(recvBootTimeNs)));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnBleDisconnected__J(
        JNIEnv* /*env*/,
        jobject /*thiz*/,
        jlong handle) {
    OnBleDisconnectedForHandle(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnDisconnected(
        JNIEnv* /*env*/,
        jobject /*thiz*/,
        jlong handle) {
    OnBleDisconnectedForHandle(handle);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnBleReady__(
        JNIEnv* env,
        jobject /*thiz*/) {
    return ToJString(
            env,
            OnBleReadyForHandle(
                    LegacyBridge().EnsureHandle(),
                    ble_time_sync::BleTimeSyncCore::CurrentBootTimeNs()));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnBleCommand__Ljava_lang_String_2J(
        JNIEnv* env,
        jobject /*thiz*/,
        jstring commandJson,
        jlong recvBootTimeNs) {
    return ToJString(
            env,
            OnBleCommandForHandle(
                    LegacyBridge().EnsureHandle(),
                    JStringToUtf8(env, commandJson),
                    static_cast<int64_t>(recvBootTimeNs)));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_ble_BleService_nativeOnBleDisconnected__(
        JNIEnv* /*env*/,
        jobject /*thiz*/) {
    const jlong handle = LegacyBridge().EnsureHandle();
    OnBleDisconnectedForHandle(handle);
    LegacyBridge().DestroyHandle();
}
