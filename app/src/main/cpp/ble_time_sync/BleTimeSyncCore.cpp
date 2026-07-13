#include "ble_time_sync/BleTimeSyncCore.h"

#include "NativeLogger.h"

#include <ctime>

namespace ble_time_sync {
namespace {

constexpr char kLogTag[] = "BleTimeSync";
constexpr int kSyncSampleTarget = 8;
constexpr int kVerifySampleTarget = 4;

}  // namespace

BleTimeSyncCore::BleTimeSyncCore() = default;

TimeSyncStep BleTimeSyncCore::StartSession(int sessionId) {
    TimeSyncStep step{};
    ResetState();
    hasSession_ = true;
    phase_ = SyncPhase::kSync;
    sessionId_ = sessionId;
    status_.phase = phase_;
    status_.sessionId = sessionId_;
    step.type = ResultType::kSessionStarted;
    step.status = status_;

    NATIVE_LOGI(kLogTag,
                "session=%d event=start_session phase=sync",
                sessionId_);
    return step;
}

TimeSyncStep BleTimeSyncCore::HandleRequest(const TimeSyncRequest& request,
                                            int64_t requestRxUtcNs,
                                            int64_t replyTxUtcNs) {
    TimeSyncStep step{};
    if (!hasSession_) {
        step.type = ResultType::kError;
        step.error = ErrorCode::kNotStarted;
        NATIVE_LOGW(kLogTag, "event=request_ignored reason=not_started");
        return step;
    }

    if (request.sessionId != sessionId_) {
        step.type = ResultType::kError;
        step.error = ErrorCode::kMismatchedReply;
        NATIVE_LOGW(kLogTag,
                    "session=%d event=request_mismatch expected_session=%d got_session=%d",
                    sessionId_,
                    sessionId_,
                    request.sessionId);
        return step;
    }

    const int currentCount = request.phase == SyncPhase::kSync
                                     ? status_.syncSampleCount
                                     : status_.verifySampleCount;
    const int targetCount = request.phase == SyncPhase::kSync
                                    ? kSyncSampleTarget
                                    : kVerifySampleTarget;
    const bool isLatestRetry = currentCount > 0 && request.sampleIndex == currentCount - 1;
    bool sequenceValid = false;
    if (request.phase == SyncPhase::kSync) {
        sequenceValid = phase_ == SyncPhase::kSync &&
                        currentCount <= kSyncSampleTarget &&
                        (request.sampleIndex == currentCount || isLatestRetry);
    } else if (request.phase == SyncPhase::kVerify) {
        const bool enteringVerify = phase_ == SyncPhase::kSync &&
                                    status_.syncSampleCount == kSyncSampleTarget &&
                                    request.sampleIndex == 0;
        const bool continuingVerify = phase_ == SyncPhase::kVerify &&
                                      currentCount <= kVerifySampleTarget &&
                                      (request.sampleIndex == currentCount || isLatestRetry);
        sequenceValid = enteringVerify || continuingVerify;
    }
    if (!sequenceValid || (currentCount >= targetCount && !isLatestRetry)) {
        step.type = ResultType::kError;
        step.error = ErrorCode::kMismatchedReply;
        NATIVE_LOGW(kLogTag,
                    "session=%d event=request_sequence_mismatch current_phase=%s request_phase=%s sample=%d expected=%d",
                    sessionId_,
                    PhaseName(phase_),
                    PhaseName(request.phase),
                    request.sampleIndex,
                    currentCount);
        return step;
    }

    phase_ = request.phase;
    status_.phase = phase_;
    status_.sessionId = sessionId_;
    if (!isLatestRetry) {
        if (request.phase == SyncPhase::kSync) {
            ++status_.syncSampleCount;
        } else {
            ++status_.verifySampleCount;
        }
    }

    step.type = ResultType::kSendReply;
    step.request = request;
    step.request.sessionId = sessionId_;
    step.status = status_;
    step.status.sessionId = sessionId_;
    step.status.phase = phase_;
    step.request.phase = phase_;
    step.status.lastSampleIndex = request.sampleIndex;

    step.request.t1Ns = request.t1Ns;
    step.status.hasLastSample = false;

    step.request.sampleIndex = request.sampleIndex;
    step.request.sessionId = sessionId_;

    step.status.lastT1Ns = request.t1Ns;
    step.status.lastT2Ns = requestRxUtcNs;
    step.status.lastT3Ns = replyTxUtcNs;
    step.status.lastT4Ns = 0;

    NATIVE_LOGI(kLogTag,
                "session=%d event=request phase=%s sample=%d t1_ns=%lld",
                sessionId_,
                PhaseName(phase_),
                request.sampleIndex,
                static_cast<long long>(request.t1Ns));
    return step;
}

TimeSyncStep BleTimeSyncCore::ApplySyncResult(const SyncResultReport& result) {
    TimeSyncStep step{};
    if (!hasSession_) {
        step.type = ResultType::kError;
        step.error = ErrorCode::kNotStarted;
        NATIVE_LOGW(kLogTag, "event=sync_result_ignored reason=not_started");
        return step;
    }

    if (result.sessionId != sessionId_) {
        step.type = ResultType::kError;
        step.error = ErrorCode::kMismatchedReply;
        NATIVE_LOGW(kLogTag,
                    "session=%d event=sync_result_mismatch expected_session=%d got_session=%d",
                    sessionId_,
                    sessionId_,
                    result.sessionId);
        return step;
    }
    if (phase_ != SyncPhase::kVerify ||
        status_.syncSampleCount != kSyncSampleTarget ||
        status_.verifySampleCount != kVerifySampleTarget ||
        result.syncSampleCount != kSyncSampleTarget ||
        result.verifySampleCount != kVerifySampleTarget) {
        step.type = ResultType::kError;
        step.error = ErrorCode::kMismatchedReply;
        NATIVE_LOGW(kLogTag,
                    "session=%d event=sync_result_sequence_mismatch phase=%s sync=%d verify=%d",
                    sessionId_,
                    PhaseName(phase_),
                    status_.syncSampleCount,
                    status_.verifySampleCount);
        return step;
    }

    phase_ = SyncPhase::kComplete;
    status_.phase = phase_;
    status_.sessionId = sessionId_;
    status_.hasAdoptedOffset = true;
    status_.adoptedOffsetNs = result.adoptedOffsetNs;
    status_.adoptedRttNs = result.adoptedRttNs;
    status_.adoptedOffsetSource = result.adoptedOffsetSource;
    status_.syncAvgOffsetNs = result.syncAvgOffsetNs;
    status_.syncAvgRttNs = result.syncAvgRttNs;
    status_.verifyAvgOffsetNs = result.verifyAvgOffsetNs;
    status_.verifyAvgRttNs = result.verifyAvgRttNs;
    status_.syncVerifyDeltaNs = result.syncVerifyDeltaNs;
    status_.syncSampleCount = result.syncSampleCount;
    status_.syncFilteredSampleCount = result.syncFilteredSampleCount;
    status_.verifySampleCount = result.verifySampleCount;
    status_.verifyFilteredSampleCount = result.verifyFilteredSampleCount;

    if (result.hasLastSampleIndex) {
        status_.lastSampleIndex = result.lastSampleIndex;
    }
    if (result.hasLastT1Ns) {
        status_.lastT1Ns = result.lastT1Ns;
    }
    if (result.hasLastT2Ns) {
        status_.lastT2Ns = result.lastT2Ns;
    }
    if (result.hasLastT3Ns) {
        status_.lastT3Ns = result.lastT3Ns;
    }
    if (result.hasLastT4Ns) {
        status_.lastT4Ns = result.lastT4Ns;
    }
    status_.hasLastSample =
            result.hasLastSampleIndex &&
            result.hasLastT1Ns &&
            result.hasLastT2Ns &&
            result.hasLastT3Ns &&
            result.hasLastT4Ns;

    step.type = ResultType::kSyncResultAccepted;
    step.status = status_;

    NATIVE_LOGI(kLogTag,
                "session=%d event=sync_result adopted_offset_ns=%lld adopted_rtt_ns=%lld source=%s",
                sessionId_,
                static_cast<long long>(status_.adoptedOffsetNs),
                static_cast<long long>(status_.adoptedRttNs),
                AdoptedOffsetSourceName(status_.adoptedOffsetSource));
    return step;
}

TimeSyncStep BleTimeSyncCore::CancelSession() {
    TimeSyncStep step{};
    if (!hasSession_) {
        return step;
    }

    phase_ = SyncPhase::kIdle;
    status_.phase = phase_;
    step.type = ResultType::kCancelled;
    step.status = status_;

    hasSession_ = false;
    NATIVE_LOGI(kLogTag,
                "session=%d event=cancel_session",
                sessionId_);
    return step;
}

void BleTimeSyncCore::Reset() {
    if (hasSession_ || phase_ != SyncPhase::kIdle || sessionId_ >= 0) {
        NATIVE_LOGI(kLogTag,
                    "session=%d event=reset phase=%s",
                    sessionId_,
                    PhaseName(phase_));
    }
    ResetState();
}

SyncPhase BleTimeSyncCore::phase() const {
    return phase_;
}

bool BleTimeSyncCore::HasSession() const {
    return hasSession_ || sessionId_ >= 0;
}

int BleTimeSyncCore::sessionId() const {
    return sessionId_;
}

const char* BleTimeSyncCore::PhaseName(SyncPhase phase) {
    switch (phase) {
        case SyncPhase::kIdle:
            return "idle";
        case SyncPhase::kSync:
            return "sync";
        case SyncPhase::kVerify:
            return "verify";
        case SyncPhase::kComplete:
            return "complete";
    }
    return "unknown";
}

const char* BleTimeSyncCore::ErrorCodeName(ErrorCode error) {
    switch (error) {
        case ErrorCode::kNone:
            return "none";
        case ErrorCode::kNotStarted:
            return "not_started";
        case ErrorCode::kMismatchedReply:
            return "mismatched_reply";
        case ErrorCode::kBleNotReady:
            return "ble_not_ready";
        case ErrorCode::kMalformedCommand:
            return "malformed_command";
    }
    return "unknown";
}

const char* BleTimeSyncCore::AdoptedOffsetSourceName(AdoptedOffsetSource source) {
    switch (source) {
        case AdoptedOffsetSource::kNone:
            return "none";
        case AdoptedOffsetSource::kSync:
            return "sync";
        case AdoptedOffsetSource::kVerify:
            return "verify";
    }
    return "unknown";
}

bool BleTimeSyncCore::ParsePhase(const std::string& value, SyncPhase* phaseOut) {
    if (phaseOut == nullptr) {
        return false;
    }
    if (value == "sync") {
        *phaseOut = SyncPhase::kSync;
        return true;
    }
    if (value == "verify") {
        *phaseOut = SyncPhase::kVerify;
        return true;
    }
    if (value == "complete") {
        *phaseOut = SyncPhase::kComplete;
        return true;
    }
    if (value == "idle") {
        *phaseOut = SyncPhase::kIdle;
        return true;
    }
    return false;
}

bool BleTimeSyncCore::ParseAdoptedOffsetSource(const std::string& value,
                                               AdoptedOffsetSource* sourceOut) {
    if (sourceOut == nullptr) {
        return false;
    }
    if (value == "sync") {
        *sourceOut = AdoptedOffsetSource::kSync;
        return true;
    }
    if (value == "verify") {
        *sourceOut = AdoptedOffsetSource::kVerify;
        return true;
    }
    if (value == "none") {
        *sourceOut = AdoptedOffsetSource::kNone;
        return true;
    }
    return false;
}

int64_t BleTimeSyncCore::CurrentBootTimeNs() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int64_t BleTimeSyncCore::CurrentRealtimeNs() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

TimeSyncStatus BleTimeSyncCore::GetStatus() const {
    return status_;
}

void BleTimeSyncCore::ResetState() {
    hasSession_ = false;
    phase_ = SyncPhase::kIdle;
    sessionId_ = -1;
    status_ = TimeSyncStatus{};
}

}  // namespace ble_time_sync
