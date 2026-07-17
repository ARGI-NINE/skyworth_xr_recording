#pragma once

#include <cstdint>
#include <string>

namespace sdk_state_bridge {

struct StateSnapshot {
    // Authoritative business state. Resource booleans below are diagnostics
    // only and must never be used to infer the operation mode.
    uint32_t operationMode = 0;
    uint32_t operationPhase = 0;
    uint64_t stateRevision = 0;
    bool engineAvailable = false;
    bool isRecording = false;
    bool stopInProgress = false;
    bool encodingEnabled = false;
    bool encodersStopped = true;
    bool autoStopRequested = false;
    bool useControllerMode = false;
    bool cameraContextAvailable = false;
    bool cameraRgbOpen = false;
    bool cameraTrackingOpen = false;
    bool cameraCtrlOpen = false;
    bool rgbFrameReady = false;
    bool trackingFrameReady = false;
    bool ctrlFrameReady = false;
    bool imuRunning = false;
    bool imuFinished = false;
    bool micRunning = false;
    bool micFinished = false;
    bool storageKnown = false;
    bool storageLow = false;
    bool canStartRecording = false;
    int64_t storageAvailableBytes = -1;
    int64_t storageThresholdBytes = 0;
    std::string captureState = "unavailable";
};

struct ErrorSnapshot {
    bool hasPending = false;
    uint64_t sequence = 0;
    int64_t unixTimeMs = 0;
    std::string source;
    std::string code;
    std::string message;
    std::string detail;
};

using StateProvider = bool (*)(StateSnapshot* out);

void RegisterStateProvider(StateProvider provider);

bool ReadStateSnapshot(StateSnapshot* out);

std::string GetStateJson();

void ReportError(const char* source,
                 const char* code,
                 const char* message,
                 const char* detail = nullptr);

void ClearError();

ErrorSnapshot PeekError();

ErrorSnapshot ConsumeError();

std::string PeekErrorJson();

std::string ConsumeErrorJson();

}  // namespace sdk_state_bridge
