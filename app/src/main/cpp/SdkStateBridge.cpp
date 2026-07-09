#include "SdkStateBridge.h"

#include "NativeLogger.h"

#include <jni.h>

#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>

namespace sdk_state_bridge {
namespace {

constexpr char kLogTag[] = "SdkStateBridge";

class StateRegistry {
public:
    void SetProvider(StateProvider provider) {
        std::lock_guard<std::mutex> lock(mutex_);
        provider_ = provider;
    }

    StateProvider GetProvider() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return provider_;
    }

private:
    mutable std::mutex mutex_;
    StateProvider provider_ = nullptr;
};

class ErrorRegistry {
public:
    void Report(const char* source,
                const char* code,
                const char* message,
                const char* detail) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.hasPending = true;
        snapshot_.sequence += 1;
        snapshot_.unixTimeMs = CurrentUnixTimeMs();
        snapshot_.source = source != nullptr ? source : "";
        snapshot_.code = code != nullptr ? code : "";
        snapshot_.message = message != nullptr ? message : "";
        snapshot_.detail = detail != nullptr ? detail : "";
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.hasPending = false;
        snapshot_.detail.clear();
        snapshot_.source.clear();
        snapshot_.code.clear();
        snapshot_.message.clear();
        snapshot_.unixTimeMs = 0;
    }

    ErrorSnapshot Peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    ErrorSnapshot Consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        ErrorSnapshot snapshot = snapshot_;
        snapshot_.hasPending = false;
        snapshot_.detail.clear();
        snapshot_.source.clear();
        snapshot_.code.clear();
        snapshot_.message.clear();
        snapshot_.unixTimeMs = 0;
        return snapshot;
    }

private:
    static int64_t CurrentUnixTimeMs() {
        struct timespec ts {};
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1000LL +
               static_cast<int64_t>(ts.tv_nsec) / 1000000LL;
    }

    mutable std::mutex mutex_;
    ErrorSnapshot snapshot_;
};

StateRegistry& GetStateRegistry() {
    static StateRegistry registry;
    return registry;
}

ErrorRegistry& GetErrorRegistry() {
    static ErrorRegistry registry;
    return registry;
}

std::string EscapeJson(const std::string& value) {
    std::ostringstream oss;
    for (char ch : value) {
        switch (ch) {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            case '\b':
                oss << "\\b";
                break;
            case '\f':
                oss << "\\f";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", ch & 0xff);
                    oss << buf;
                } else {
                    oss << ch;
                }
                break;
        }
    }
    return oss.str();
}

std::string BuildErrorJson(const ErrorSnapshot& error) {
    if (!error.hasPending) {
        return std::string();
    }

    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"sdk_error\""
        << ",\"sequence\":" << error.sequence
        << ",\"unix_time_ms\":" << error.unixTimeMs
        << ",\"source\":\"" << EscapeJson(error.source) << "\""
        << ",\"code\":\"" << EscapeJson(error.code) << "\""
        << ",\"message\":\"" << EscapeJson(error.message) << "\"";
    if (!error.detail.empty()) {
        oss << ",\"detail\":\"" << EscapeJson(error.detail) << "\"";
    }
    oss << "}";
    return oss.str();
}

}  // namespace

void RegisterStateProvider(StateProvider provider) {
    GetStateRegistry().SetProvider(provider);
}

bool ReadStateSnapshot(StateSnapshot* out) {
    if (out == nullptr) {
        return false;
    }

    StateSnapshot snapshot;
    const StateProvider provider = GetStateRegistry().GetProvider();
    const bool engineAvailable = provider != nullptr && provider(&snapshot);
    if (engineAvailable) {
        snapshot.engineAvailable = true;
        *out = snapshot;
        return true;
    }

    *out = StateSnapshot{};
    return false;
}

std::string GetStateJson() {
    StateSnapshot snapshot;
    const StateProvider provider = GetStateRegistry().GetProvider();
    const bool providerRegistered = provider != nullptr;
    const bool engineAvailable = ReadStateSnapshot(&snapshot);

    if (snapshot.engineAvailable) {
        snapshot.canStartRecording =
                !snapshot.isRecording &&
                !snapshot.stopInProgress &&
                (!snapshot.storageKnown || !snapshot.storageLow);
    } else {
        snapshot.canStartRecording = false;
    }

    const ErrorSnapshot pendingError = GetErrorRegistry().Peek();

    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"sdk_state\""
        << ",\"provider_registered\":" << (providerRegistered ? "true" : "false")
        << ",\"engine_available\":" << (snapshot.engineAvailable ? "true" : "false")
        << ",\"capture_state\":\"" << EscapeJson(snapshot.captureState) << "\""
        << ",\"is_recording\":" << (snapshot.isRecording ? "true" : "false")
        << ",\"stop_in_progress\":" << (snapshot.stopInProgress ? "true" : "false")
        << ",\"encoding_enabled\":" << (snapshot.encodingEnabled ? "true" : "false")
        << ",\"encoders_stopped\":" << (snapshot.encodersStopped ? "true" : "false")
        << ",\"auto_stop_requested\":" << (snapshot.autoStopRequested ? "true" : "false")
        << ",\"use_controller_mode\":" << (snapshot.useControllerMode ? "true" : "false")
        << ",\"can_start_recording\":" << (snapshot.canStartRecording ? "true" : "false")
        << ",\"storage_known\":" << (snapshot.storageKnown ? "true" : "false")
        << ",\"storage_low\":" << (snapshot.storageLow ? "true" : "false")
        << ",\"storage_available_bytes\":" << snapshot.storageAvailableBytes
        << ",\"storage_threshold_bytes\":" << snapshot.storageThresholdBytes
        << ",\"has_pending_error\":" << (pendingError.hasPending ? "true" : "false")
        << ",\"pending_error_sequence\":" << pendingError.sequence
        << "}";
    return oss.str();
}

void ReportError(const char* source,
                 const char* code,
                 const char* message,
                 const char* detail) {
    GetErrorRegistry().Report(source, code, message, detail);
    NATIVE_LOGW(kLogTag,
                "event=report_error source=%s code=%s message=%s detail=%s",
                source != nullptr ? source : "",
                code != nullptr ? code : "",
                message != nullptr ? message : "",
                detail != nullptr ? detail : "");
}

void ClearError() {
    GetErrorRegistry().Clear();
}

ErrorSnapshot PeekError() {
    return GetErrorRegistry().Peek();
}

ErrorSnapshot ConsumeError() {
    return GetErrorRegistry().Consume();
}

std::string PeekErrorJson() {
    return BuildErrorJson(PeekError());
}

std::string ConsumeErrorJson() {
    return BuildErrorJson(ConsumeError());
}

}  // namespace sdk_state_bridge

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeGetSdkStateJson(
        JNIEnv* env,
        jobject /*thiz*/) {
    const std::string json = sdk_state_bridge::GetStateJson();
    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativePeekSdkErrorJson(
        JNIEnv* env,
        jobject /*thiz*/) {
    const std::string json = sdk_state_bridge::PeekErrorJson();
    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeConsumeSdkErrorJson(
        JNIEnv* env,
        jobject /*thiz*/) {
    const std::string json = sdk_state_bridge::ConsumeErrorJson();
    return env->NewStringUTF(json.c_str());
}
