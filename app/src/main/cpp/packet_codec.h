#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace egocollect {

constexpr uint16_t kEgMagic = 0x4547;
constexpr std::size_t kEgHeaderSize = 6;
constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kMaxFramePayloadBytes = 16U * 1024U * 1024U;

enum class CommandType : uint32_t {
    kUnknown = 0,
    kStartCollect = 1,
    kStopCollect = 2,
    kStartVideo = 3,
    kStopVideo = 4,
    kHeartbeat = 5,
    kGetParam = 10,
    kSetParam = 11,
    kReboot = 20,
    // Legacy values kept only for wire compatibility. They are not part of the
    // current PROTOCOL.md command surface and should decode as unknown.
    kFactoryReset = 21,
    kOtaStart = 30,
    kOtaData = 31,
    kOtaEnd = 32,
};

enum class ResultCode : uint32_t {
    kOk = 0,
    kUnknownCommand = 100,
    kInvalidParam = 101,
    kDeviceBusy = 102,
    kStorageFull = 200,
    kWifiDisconnected = 300,
    kCameraFailed = 400,
    kMicrophoneFailed = 401,
    kInternal = 500,
    // Legacy value kept only for wire compatibility. It should not be emitted
    // on the current protocol surface.
    kOtaCheckFailed = 501,
};

inline bool IsSupportedCommandType(CommandType type) {
    switch (type) {
        case CommandType::kStartCollect:
        case CommandType::kStopCollect:
        case CommandType::kStartVideo:
        case CommandType::kStopVideo:
        case CommandType::kHeartbeat:
        case CommandType::kGetParam:
        case CommandType::kSetParam:
        case CommandType::kReboot:
            return true;
        case CommandType::kUnknown:
        case CommandType::kFactoryReset:
        case CommandType::kOtaStart:
        case CommandType::kOtaData:
        case CommandType::kOtaEnd:
            return false;
    }
    return false;
}

inline bool IsSupportedResultCode(ResultCode code) {
    switch (code) {
        case ResultCode::kOk:
        case ResultCode::kUnknownCommand:
        case ResultCode::kInvalidParam:
        case ResultCode::kDeviceBusy:
        case ResultCode::kStorageFull:
        case ResultCode::kWifiDisconnected:
        case ResultCode::kCameraFailed:
        case ResultCode::kMicrophoneFailed:
        case ResultCode::kInternal:
            return true;
        case ResultCode::kOtaCheckFailed:
            return false;
    }
    return false;
}

enum class WorkingState : uint32_t {
    kIdle = 0,
    kCollecting = 1,
    kUploading = 2,
    kSleeping = 3,
    kFault = 4,
};

enum class FaultLevel : uint32_t {
    kInfo = 0,
    kWarn = 1,
    kError = 2,
    kFatal = 3,
};

enum class ConnectionState : uint32_t {
    kDisconnected = 0,
    kConnecting = 1,
    kConnected = 2,
    kReconnecting = 3,
};

struct CommandPacket {
    uint32_t seq = 0;
    uint64_t timestampMs = 0;
    uint32_t version = 0;
    CommandType command = CommandType::kUnknown;
    std::map<std::string, std::string> params;
};

struct BatteryInfo {
    bool hasValue = false;
    uint32_t level = 0;
    bool charging = false;
    float voltage = 0.0f;
    bool hasTemperature = false;
    float temperature = 0.0f;
};

struct WifiInfo {
    bool hasValue = false;
    int32_t rssi = 0;
    std::string ssid;
    uint32_t channel = 0;
};

struct StorageInfo {
    bool hasValue = false;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
};

struct PeripheralState {
    std::string name;
    bool connected = false;
    bool healthy = false;
};

struct StatusMessage {
    BatteryInfo battery;
    WifiInfo wifi;
    WorkingState workingState = WorkingState::kIdle;
    StorageInfo storage;
    std::vector<PeripheralState> peripherals;
};

struct ResponseMessage {
    uint32_t requestSeq = 0;
    ResultCode code = ResultCode::kOk;
    std::string message;
    std::map<std::string, std::string> data;
};

struct FaultEvent {
    std::string code;
    std::string description;
    FaultLevel level = FaultLevel::kInfo;
    uint64_t raiseTimeMs = 0;
};

struct FaultClearedEvent {
    std::string code;
};

struct ConnectionChangedEvent {
    ConnectionState state = ConnectionState::kDisconnected;
};

struct EventMessage {
    enum class Kind {
        kNone,
        kFault,
        kFaultCleared,
        kConnectionChanged,
    };

    Kind kind = Kind::kNone;
    FaultEvent fault;
    FaultClearedEvent faultCleared;
    ConnectionChangedEvent connectionChanged;
};

bool ParseCommandPacket(const std::string& payload,
                        CommandPacket* out,
                        std::string* error);

bool SerializeStatusPacket(uint32_t seq,
                           uint64_t timestampMs,
                           const StatusMessage& status,
                           std::string* out);

bool SerializeResponsePacket(uint32_t seq,
                             uint64_t timestampMs,
                             const ResponseMessage& response,
                             std::string* out);

bool SerializeEventPacket(uint32_t seq,
                          uint64_t timestampMs,
                          const EventMessage& event,
                          std::string* out);

void PackEgFrame(const std::string& payload, std::string* out);

bool TryConvertLengthPrefixedToAnnexB(const uint8_t* data,
                                      std::size_t size,
                                      std::string* out);

}  // namespace egocollect
