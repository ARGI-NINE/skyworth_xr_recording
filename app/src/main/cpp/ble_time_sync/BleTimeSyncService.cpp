#include "ble_time_sync/BleTimeSyncService.h"

#include "NativeLogger.h"
#include "ble_time_sync/BleTimeSyncProtocol.h"

namespace ble_time_sync {
namespace {

constexpr char kLogTag[] = "BleTimeSync";

TimeSyncReply BuildReply(const TimeSyncRequest& request,
                         int64_t requestRxUtcNs,
                         int64_t replyTxUtcNs) {
    TimeSyncReply reply{};
    reply.phase = request.phase;
    reply.sessionId = request.sessionId;
    reply.sampleIndex = request.sampleIndex;
    reply.t1Ns = request.t1Ns;
    reply.t2Ns = requestRxUtcNs;
    reply.t3Ns = replyTxUtcNs;
    return reply;
}

}  // namespace

std::string BleTimeSyncService::OnBleReady(int64_t nowBootTimeNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    bleReady_ = true;
    NATIVE_LOGI(kLogTag,
                "event=ble_ready now_boottime_ns=%lld",
                static_cast<long long>(nowBootTimeNs));
    return std::string();
}

std::string BleTimeSyncService::OnControlCommand(const std::string& commandJson,
                                                 int64_t recvBootTimeNs) {
    ControlCommand parsed{};
    if (!ParseControlCommand(commandJson, recvBootTimeNs, &parsed)) {
        NATIVE_LOGW(kLogTag,
                    "event=command_parse_failed raw=%s",
                    commandJson.c_str());
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = ErrorCode::kMalformedCommand;
        lastErrorDetail_ = "command missing required fields or _ns is not a decimal string";
        return BuildErrorJson(ErrorCode::kMalformedCommand,
                              "sync",
                              core_.sessionId(),
                              lastErrorDetail_);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    switch (parsed.type) {
        case ControlCommandType::kStartTimeSync:
            if (!bleReady_) {
                lastError_ = ErrorCode::kBleNotReady;
                lastErrorDetail_ = "BLE time sync notifications are not ready yet";
                return BuildErrorJson(ErrorCode::kBleNotReady,
                                      parsed.op,
                                      parsed.sessionId,
                                      lastErrorDetail_);
            }
            cancelled_ = false;
            lastError_ = ErrorCode::kNone;
            lastErrorDetail_.clear();
            EncodeStep(core_.StartSession(parsed.sessionId));
            return BuildStartedJson(parsed.sessionId);
        case ControlCommandType::kTimeSyncRequest: {
            if (!bleReady_) {
                lastError_ = ErrorCode::kBleNotReady;
                lastErrorDetail_ = "BLE time sync notifications are not ready yet";
                return BuildErrorJson(ErrorCode::kBleNotReady,
                                      parsed.op,
                                      parsed.sessionId,
                                      lastErrorDetail_);
            }
            cancelled_ = false;
            lastError_ = ErrorCode::kNone;
            lastErrorDetail_.clear();

            const int64_t requestRxUtcNs = BleTimeSyncCore::CurrentRealtimeNs();
            const int64_t replyTxUtcNs = BleTimeSyncCore::CurrentRealtimeNs();
            const TimeSyncStep step =
                    core_.HandleRequest(parsed.request, requestRxUtcNs, replyTxUtcNs);
            if (step.type == ResultType::kError) {
                lastError_ = step.error;
                lastErrorDetail_.clear();
                return BuildErrorJson(step.error,
                                      parsed.op,
                                      parsed.sessionId);
            }
            return BuildReplyJson(BuildReply(parsed.request, requestRxUtcNs, replyTxUtcNs));
        }
        case ControlCommandType::kSyncResult: {
            if (!bleReady_) {
                lastError_ = ErrorCode::kBleNotReady;
                lastErrorDetail_ = "BLE time sync notifications are not ready yet";
                return BuildErrorJson(ErrorCode::kBleNotReady,
                                      parsed.op,
                                      parsed.sessionId,
                                      lastErrorDetail_);
            }
            cancelled_ = false;
            lastError_ = ErrorCode::kNone;
            lastErrorDetail_.clear();
            const TimeSyncStep step = core_.ApplySyncResult(parsed.syncResult);
            if (step.type == ResultType::kError) {
                lastError_ = step.error;
                return BuildErrorJson(step.error,
                                      parsed.op,
                                      parsed.sessionId);
            }
            return std::string();
        }
        case ControlCommandType::kCancelSync:
            {
                std::string errorJson;
                if (!ValidateSessionIdLocked(parsed.sessionId, parsed.op, &errorJson)) {
                    return errorJson;
                }
            }
            cancelled_ = true;
            lastError_ = ErrorCode::kNone;
            lastErrorDetail_.clear();
            EncodeStep(core_.CancelSession());
            return BuildStatusJson(CurrentStatusLocked(),
                                   parsed.op,
                                   "cancelled",
                                   lastError_,
                                   lastErrorDetail_);
        case ControlCommandType::kGetTimeSyncStatus:
            {
                std::string errorJson;
                if (!ValidateSessionIdLocked(parsed.sessionId, parsed.op, &errorJson)) {
                    return errorJson;
                }
            }
            return BuildStatusJson(CurrentStatusLocked(),
                                   parsed.op,
                                   CurrentQueryStatusLocked(),
                                   lastError_,
                                   lastErrorDetail_);
    }

    return std::string();
}

void BleTimeSyncService::OnDisconnected() {
    std::lock_guard<std::mutex> lock(mutex_);
    bleReady_ = false;
    cancelled_ = false;
    core_.Reset();
    lastError_ = ErrorCode::kNone;
    lastErrorDetail_.clear();
    NATIVE_LOGI(kLogTag, "event=disconnected");
}

std::string BleTimeSyncService::EncodeStep(const TimeSyncStep& step) {
    switch (step.type) {
        case ResultType::kNone:
        case ResultType::kSessionStarted:
        case ResultType::kSendReply:
        case ResultType::kSyncResultAccepted:
        case ResultType::kCancelled:
            return std::string();
        case ResultType::kError:
            lastError_ = step.error;
            lastErrorDetail_.clear();
            return BuildErrorJson(step.error, "sync", core_.sessionId());
    }
    return std::string();
}

bool BleTimeSyncService::ValidateSessionIdLocked(int sessionId,
                                                 const std::string& op,
                                                 std::string* errorJsonOut) {
    if (errorJsonOut == nullptr) {
        return false;
    }
    const int currentSessionId = core_.sessionId();
    if (currentSessionId < 0) {
        lastError_ = ErrorCode::kNotStarted;
        lastErrorDetail_.clear();
        *errorJsonOut = BuildErrorJson(lastError_, op, sessionId);
        return false;
    }
    if (sessionId != currentSessionId) {
        lastError_ = ErrorCode::kMismatchedReply;
        lastErrorDetail_.clear();
        *errorJsonOut = BuildErrorJson(lastError_, op, sessionId);
        return false;
    }
    return true;
}

TimeSyncStatus BleTimeSyncService::CurrentStatusLocked() const {
    return core_.GetStatus();
}

std::string BleTimeSyncService::CurrentQueryStatusLocked() const {
    if (lastError_ != ErrorCode::kNone) {
        return "failed";
    }
    if (cancelled_) {
        return "cancelled";
    }
    switch (core_.phase()) {
        case SyncPhase::kSync:
            return "sync";
        case SyncPhase::kVerify:
            return "verify";
        case SyncPhase::kComplete:
            return "synced";
        case SyncPhase::kIdle:
            break;
    }
    return "idle";
}

}  // namespace ble_time_sync
