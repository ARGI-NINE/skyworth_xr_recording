#pragma once

#include <cstdint>
#include <string>

namespace platform_state_bridge {

struct PlatformSnapshot {
    bool hasBattery = false;
    int32_t batteryLevel = -1;
    bool batteryCharging = false;
    float batteryVoltage = 0.0f;
    bool hasBatteryTemperature = false;
    float batteryTemperature = 0.0f;
    bool hasThermalStatus = false;
    int32_t thermalStatus = 0;

    bool hasWifi = false;
    bool wifiConnected = false;
    std::string wifiSsid;
    int32_t wifiRssi = 0;
    uint32_t wifiChannel = 0;
    std::string wifiIpAddress;

    int64_t updateTimeMs = 0;
};

void UpdatePlatformSnapshot(const PlatformSnapshot& snapshot);

PlatformSnapshot GetPlatformSnapshot();

}  // namespace platform_state_bridge
