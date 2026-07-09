#include "platform_state_bridge.h"

#include <jni.h>

#include <mutex>

namespace platform_state_bridge {
namespace {

class SnapshotRegistry {
public:
    void Update(const PlatformSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = snapshot;
    }

    PlatformSnapshot Get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    PlatformSnapshot snapshot_;
};

SnapshotRegistry& Registry() {
    static SnapshotRegistry registry;
    return registry;
}

}  // namespace

void UpdatePlatformSnapshot(const PlatformSnapshot& snapshot) {
    Registry().Update(snapshot);
}

PlatformSnapshot GetPlatformSnapshot() {
    return Registry().Get();
}

}  // namespace platform_state_bridge

namespace {

std::string JStringToStdString(JNIEnv* env, jstring value) {
    if (env == nullptr || value == nullptr) {
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

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeUpdatePlatformState(
        JNIEnv* env,
        jobject /*thiz*/,
        jboolean hasBattery,
        jint batteryLevel,
        jboolean batteryCharging,
        jfloat batteryVoltage,
        jboolean hasBatteryTemperature,
        jfloat batteryTemperature,
        jboolean hasWifi,
        jboolean wifiConnected,
        jstring wifiSsid,
        jint wifiRssi,
        jint wifiChannel,
        jstring wifiIpAddress,
        jlong updateTimeMs) {
    platform_state_bridge::PlatformSnapshot snapshot;
    snapshot.hasBattery = hasBattery == JNI_TRUE;
    snapshot.batteryLevel = batteryLevel;
    snapshot.batteryCharging = batteryCharging == JNI_TRUE;
    snapshot.batteryVoltage = batteryVoltage;
    snapshot.hasBatteryTemperature = hasBatteryTemperature == JNI_TRUE;
    snapshot.batteryTemperature = batteryTemperature;
    snapshot.hasWifi = hasWifi == JNI_TRUE;
    snapshot.wifiConnected = wifiConnected == JNI_TRUE;
    snapshot.wifiSsid = JStringToStdString(env, wifiSsid);
    snapshot.wifiRssi = wifiRssi;
    snapshot.wifiChannel = wifiChannel > 0 ? static_cast<uint32_t>(wifiChannel) : 0U;
    snapshot.wifiIpAddress = JStringToStdString(env, wifiIpAddress);
    snapshot.updateTimeMs = static_cast<int64_t>(updateTimeMs);
    platform_state_bridge::UpdatePlatformSnapshot(snapshot);
}
