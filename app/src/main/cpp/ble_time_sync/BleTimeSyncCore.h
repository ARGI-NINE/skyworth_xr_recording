#pragma once

#include <cstdint>
#include <string>

namespace ble_time_sync {

enum class SyncPhase {
    kIdle,
    kSync,
    kVerify,
    kComplete,
};

enum class ResultType {
    kNone,
    kSessionStarted,
    kSendReply,
    kSyncResultAccepted,
    kCancelled,
    kError,
};

enum class ErrorCode {
    kNone,
    kNotStarted,
    kMismatchedReply,
    kBleNotReady,
    kMalformedCommand,
};

enum class AdoptedOffsetSource {
    kNone,
    kSync,
    kVerify,
};

struct TimeSyncRequest {
    SyncPhase phase = SyncPhase::kIdle;
    int sessionId = -1;
    int sampleIndex = -1;
    int64_t t1Ns = 0;
};

struct TimeSyncReply {
    SyncPhase phase = SyncPhase::kIdle;
    int sessionId = -1;
    int sampleIndex = -1;
    int64_t t1Ns = 0;
    int64_t t2Ns = 0;
    int64_t t3Ns = 0;
};

struct SyncResultReport {
    int sessionId = -1;
    bool hasLastSampleIndex = false;
    int lastSampleIndex = -1;
    bool hasLastT1Ns = false;
    int64_t lastT1Ns = 0;
    bool hasLastT2Ns = false;
    int64_t lastT2Ns = 0;
    bool hasLastT3Ns = false;
    int64_t lastT3Ns = 0;
    bool hasLastT4Ns = false;
    int64_t lastT4Ns = 0;
    int64_t adoptedOffsetNs = 0;
    int64_t adoptedRttNs = 0;
    AdoptedOffsetSource adoptedOffsetSource = AdoptedOffsetSource::kNone;
    int64_t syncAvgOffsetNs = 0;
    int64_t syncAvgRttNs = 0;
    int64_t verifyAvgOffsetNs = 0;
    int64_t verifyAvgRttNs = 0;
    int64_t syncVerifyDeltaNs = 0;
    int syncSampleCount = 0;
    int syncFilteredSampleCount = 0;
    int verifySampleCount = 0;
    int verifyFilteredSampleCount = 0;
};

struct TimeSyncStatus {
    SyncPhase phase = SyncPhase::kIdle;
    int sessionId = -1;
    int64_t syncAvgOffsetNs = 0;
    int64_t syncAvgRttNs = 0;
    int64_t verifyAvgOffsetNs = 0;
    int64_t verifyAvgRttNs = 0;
    int64_t syncVerifyDeltaNs = 0;
    int64_t adoptedOffsetNs = 0;
    int64_t adoptedRttNs = 0;
    AdoptedOffsetSource adoptedOffsetSource = AdoptedOffsetSource::kNone;
    int syncSampleCount = 0;
    int syncFilteredSampleCount = 0;
    int verifySampleCount = 0;
    int verifyFilteredSampleCount = 0;
    bool hasAdoptedOffset = false;
    bool hasLastSample = false;
    int lastSampleIndex = -1;
    int64_t lastT1Ns = 0;
    int64_t lastT2Ns = 0;
    int64_t lastT3Ns = 0;
    int64_t lastT4Ns = 0;
    bool hasClientReportedMetrics = false;
    int64_t clientReportedOffsetNs = 0;
    int64_t clientReportedRttNs = 0;
    int64_t clientReportedDelayNs = 0;
};

struct TimeSyncStep {
    ResultType type = ResultType::kNone;
    ErrorCode error = ErrorCode::kNone;
    TimeSyncRequest request{};
    TimeSyncStatus status{};
};

class BleTimeSyncCore {
public:
    BleTimeSyncCore();
    TimeSyncStep StartSession(int sessionId);
    TimeSyncStep HandleRequest(const TimeSyncRequest& request,
                               int64_t requestRxUtcNs,
                               int64_t replyTxUtcNs);
    TimeSyncStep ApplySyncResult(const SyncResultReport& result);
    TimeSyncStep CancelSession();
    TimeSyncStatus GetStatus() const;
    void Reset();

    SyncPhase phase() const;
    bool HasSession() const;
    int sessionId() const;

    static const char* PhaseName(SyncPhase phase);
    static const char* ErrorCodeName(ErrorCode error);
    static const char* AdoptedOffsetSourceName(AdoptedOffsetSource source);
    static bool ParsePhase(const std::string& value, SyncPhase* phaseOut);
    static bool ParseAdoptedOffsetSource(const std::string& value,
                                         AdoptedOffsetSource* sourceOut);
    static int64_t CurrentBootTimeNs();
    static int64_t CurrentRealtimeNs();

private:
    void ResetState();

private:
    bool hasSession_ = false;
    SyncPhase phase_ = SyncPhase::kIdle;
    int sessionId_ = -1;
    TimeSyncStatus status_{};
};

}  // namespace ble_time_sync
