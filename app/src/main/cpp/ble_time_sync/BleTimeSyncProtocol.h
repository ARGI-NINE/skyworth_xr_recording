#pragma once

#include "ble_time_sync/BleTimeSyncCore.h"

#include <cstdint>
#include <string>

namespace ble_time_sync {

enum class ControlCommandType {
    kStartTimeSync,
    kCancelSync,
    kTimeSyncRequest,
    kSyncResult,
    kGetTimeSyncStatus,
};

struct ControlCommand {
    ControlCommandType type;
    std::string op;
    std::string status;
    std::string action;
    std::string query;
    int sessionId = -1;
    TimeSyncRequest request{};
    SyncResultReport syncResult{};
};

bool ParseControlCommand(const std::string& commandJson,
                         int64_t recvBootTimeNs,
                         ControlCommand* command);
std::string BuildStartedJson(int sessionId);
std::string BuildReplyJson(const TimeSyncReply& reply);
std::string BuildStatusJson(const TimeSyncStatus& status,
                            const std::string& op,
                            const std::string& statusValue,
                            ErrorCode lastError,
                            const std::string& lastErrorDetail);
std::string BuildErrorJson(ErrorCode error,
                           const std::string& op,
                           int sessionId = -1,
                           const std::string& detail = std::string());

}  // namespace ble_time_sync
