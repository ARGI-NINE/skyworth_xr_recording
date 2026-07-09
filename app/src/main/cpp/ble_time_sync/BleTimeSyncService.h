#pragma once

#include "ble_time_sync/BleTimeSyncCore.h"

#include <mutex>
#include <string>

namespace ble_time_sync {

class BleTimeSyncService {
public:
    BleTimeSyncService() = default;

    std::string OnBleReady(int64_t nowBootTimeNs);
    std::string OnControlCommand(const std::string& commandJson, int64_t recvBootTimeNs);
    void OnDisconnected();

private:
    std::string EncodeStep(const TimeSyncStep& step);
    bool ValidateSessionIdLocked(int sessionId,
                                 const std::string& op,
                                 std::string* errorJsonOut);
    TimeSyncStatus CurrentStatusLocked() const;
    std::string CurrentQueryStatusLocked() const;

private:
    mutable std::mutex mutex_;
    BleTimeSyncCore core_;
    bool bleReady_ = false;
    bool cancelled_ = false;
    ErrorCode lastError_ = ErrorCode::kNone;
    std::string lastErrorDetail_;
};

}  // namespace ble_time_sync
