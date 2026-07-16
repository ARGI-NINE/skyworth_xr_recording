#include "ble_time_sync/BleTimeSyncProtocol.h"

#include <cctype>
#include <cstdio>
#include <limits>
#include <sstream>

namespace ble_time_sync {
namespace {

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string NsToDecimalString(int64_t valueNs) {
    std::ostringstream oss;
    oss << valueNs;
    return oss.str();
}

void AppendJsonStringField(std::ostringstream& oss,
                           const char* key,
                           const std::string& value) {
    oss << ",\"" << key << "\":\"" << EscapeJson(value) << "\"";
}

void AppendJsonInt64StringField(std::ostringstream& oss,
                                const char* key,
                                int64_t value) {
    AppendJsonStringField(oss, key, NsToDecimalString(value));
}

bool SkipToFieldValue(const std::string& json,
                      const char* key,
                      std::size_t* valuePos) {
    const std::string token = "\"" + std::string(key) + "\"";
    const std::size_t keyPos = json.find(token);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t pos = keyPos + token.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != ':') {
        return false;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return false;
    }

    *valuePos = pos;
    return true;
}

bool FindStringField(const std::string& json, const char* key, std::string* valueOut) {
    std::size_t pos = 0;
    if (!SkipToFieldValue(json, key, &pos) || json[pos] != '"') {
        return false;
    }

    ++pos;
    std::string value;
    bool escaping = false;
    for (; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (escaping) {
            value.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            *valueOut = value;
            return true;
        }
        value.push_back(ch);
    }

    return false;
}

bool ParseInt64(const std::string& value, int64_t* valueOut) {
    if (value.empty() || valueOut == nullptr) {
        return false;
    }
    long long parsed = 0;
    if (std::sscanf(value.c_str(), "%lld", &parsed) != 1) {
        return false;
    }
    *valueOut = static_cast<int64_t>(parsed);
    return true;
}

bool IsStrictDecimalString(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    std::size_t pos = 0;
    if (value[pos] == '+' || value[pos] == '-') {
        ++pos;
    }
    if (pos >= value.size()) {
        return false;
    }
    for (; pos < value.size(); ++pos) {
        if (!std::isdigit(static_cast<unsigned char>(value[pos]))) {
            return false;
        }
    }
    return true;
}

bool FindInt64Field(const std::string& json, const char* key, int64_t* valueOut) {
    std::size_t pos = 0;
    if (!SkipToFieldValue(json, key, &pos)) {
        return false;
    }

    bool quoted = false;
    if (json[pos] == '"') {
        quoted = true;
        ++pos;
    }

    std::size_t end = pos;
    if (end < json.size() && (json[end] == '-' || json[end] == '+')) {
        ++end;
    }
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }
    if (end == pos) {
        return false;
    }
    if (quoted && (end >= json.size() || json[end] != '"')) {
        return false;
    }

    return ParseInt64(json.substr(pos, end - pos), valueOut);
}

bool FindStrictNsField(const std::string& json, const char* key, int64_t* valueOut) {
    std::string value;
    if (!FindStringField(json, key, &value)) {
        return false;
    }
    if (!IsStrictDecimalString(value)) {
        return false;
    }
    return ParseInt64(value, valueOut);
}

bool HasField(const std::string& json, const char* key) {
    std::size_t pos = 0;
    return SkipToFieldValue(json, key, &pos);
}

bool ParseRequiredIntField(const std::string& json,
                           const char* key,
                           int* valueOut) {
    int64_t value = 0;
    if (!FindInt64Field(json, key, &value) || valueOut == nullptr) {
        return false;
    }
    *valueOut = static_cast<int>(value);
    return true;
}

bool ParseRequiredSessionIdField(const std::string& json, int* valueOut) {
    int64_t value = 0;
    if (!FindInt64Field(json, "session_id", &value) ||
        valueOut == nullptr ||
        value < 0 ||
        value > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    *valueOut = static_cast<int>(value);
    return true;
}

bool ParseOptionalIntField(const std::string& json,
                           const char* key,
                           bool* hasValueOut,
                           int* valueOut) {
    int64_t value = 0;
    if (!FindInt64Field(json, key, &value)) {
        if (hasValueOut != nullptr) {
            *hasValueOut = false;
        }
        return true;
    }
    if (hasValueOut != nullptr) {
        *hasValueOut = true;
    }
    if (valueOut != nullptr) {
        *valueOut = static_cast<int>(value);
    }
    return true;
}

bool ParseOptionalNsField(const std::string& json,
                          const char* key,
                          bool* hasValueOut,
                          int64_t* valueOut) {
    int64_t value = 0;
    if (!HasField(json, key)) {
        if (hasValueOut != nullptr) {
            *hasValueOut = false;
        }
        return true;
    }
    if (!FindStrictNsField(json, key, &value)) {
        return false;
    }
    if (hasValueOut != nullptr) {
        *hasValueOut = true;
    }
    if (valueOut != nullptr) {
        *valueOut = value;
    }
    return true;
}

std::string QueryStatusName(const TimeSyncStatus& status,
                            ErrorCode lastError) {
    if (lastError != ErrorCode::kNone) {
        return "failed";
    }
    switch (status.phase) {
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

void AppendStatusFields(std::ostringstream& oss,
                        const TimeSyncStatus& status,
                        ErrorCode lastError,
                        const std::string& lastErrorDetail) {
    if (status.sessionId >= 0) {
        oss << ",\"session_id\":" << status.sessionId;
    }
    AppendJsonStringField(oss, "phase", BleTimeSyncCore::PhaseName(status.phase));
    if (status.hasLastSample) {
        oss << ",\"last_sample_index\":" << status.lastSampleIndex;
        AppendJsonInt64StringField(oss, "last_t1_ns", status.lastT1Ns);
        AppendJsonInt64StringField(oss, "last_t2_ns", status.lastT2Ns);
        AppendJsonInt64StringField(oss, "last_t3_ns", status.lastT3Ns);
        AppendJsonInt64StringField(oss, "last_t4_ns", status.lastT4Ns);
    }
    if (status.hasAdoptedOffset) {
        AppendJsonInt64StringField(oss, "adopted_offset_ns", status.adoptedOffsetNs);
        AppendJsonInt64StringField(oss, "adopted_rtt_ns", status.adoptedRttNs);
        AppendJsonStringField(oss,
                              "adopted_offset_source",
                              BleTimeSyncCore::AdoptedOffsetSourceName(status.adoptedOffsetSource));
    }
    AppendJsonInt64StringField(oss, "sync_avg_offset_ns", status.syncAvgOffsetNs);
    AppendJsonInt64StringField(oss, "sync_avg_rtt_ns", status.syncAvgRttNs);
    AppendJsonInt64StringField(oss, "verify_avg_offset_ns", status.verifyAvgOffsetNs);
    AppendJsonInt64StringField(oss, "verify_avg_rtt_ns", status.verifyAvgRttNs);
    AppendJsonInt64StringField(oss, "sync_verify_delta_ns", status.syncVerifyDeltaNs);
    oss << ",\"sync_sample_count\":" << status.syncSampleCount
        << ",\"sync_filtered_sample_count\":" << status.syncFilteredSampleCount
        << ",\"verify_sample_count\":" << status.verifySampleCount
        << ",\"verify_filtered_sample_count\":" << status.verifyFilteredSampleCount;
    if (lastError != ErrorCode::kNone) {
        AppendJsonStringField(oss, "error", BleTimeSyncCore::ErrorCodeName(lastError));
    }
    if (!lastErrorDetail.empty()) {
        AppendJsonStringField(oss, "message", lastErrorDetail);
    }
}

}  // namespace

bool ParseControlCommand(const std::string& commandJson,
                         int64_t /*recvBootTimeNs*/,
                         ControlCommand* command) {
    if (command == nullptr) {
        return false;
    }

    *command = ControlCommand{};

    std::string type;
    if (!FindStringField(commandJson, "type", &type)) {
        return false;
    }
    if (!FindStringField(commandJson, "op", &command->op)) {
        return false;
    }
    FindStringField(commandJson, "status", &command->status);
    FindStringField(commandJson, "action", &command->action);
    FindStringField(commandJson, "query", &command->query);

    if (type == "start_time_sync") {
        if (command->op != "sync" ||
            command->action != "start" ||
            !ParseRequiredSessionIdField(commandJson, &command->sessionId)) {
            return false;
        }
        command->type = ControlCommandType::kStartTimeSync;
        return true;
    }

    if (type == "time_sync_request") {
        std::string phaseValue;
        if (command->op != "sync" ||
            !ParseRequiredSessionIdField(commandJson, &command->sessionId) ||
            !FindStringField(commandJson, "phase", &phaseValue) ||
            !ParseRequiredIntField(commandJson, "sample_index", &command->request.sampleIndex) ||
            !FindStrictNsField(commandJson, "t1_ns", &command->request.t1Ns) ||
            !BleTimeSyncCore::ParsePhase(phaseValue, &command->request.phase) ||
            (command->request.phase != SyncPhase::kSync &&
             command->request.phase != SyncPhase::kVerify)) {
            return false;
        }
        command->type = ControlCommandType::kTimeSyncRequest;
        command->request.sessionId = command->sessionId;
        return true;
    }

    if (type == "sync_result") {
        std::string sourceValue;
        if (command->op != "sync" ||
            command->status != "synced" ||
            !ParseRequiredSessionIdField(commandJson, &command->sessionId) ||
            !FindStrictNsField(commandJson, "adopted_offset_ns", &command->syncResult.adoptedOffsetNs) ||
            !FindStrictNsField(commandJson, "adopted_rtt_ns", &command->syncResult.adoptedRttNs) ||
            !FindStringField(commandJson, "adopted_offset_source", &sourceValue) ||
            !BleTimeSyncCore::ParseAdoptedOffsetSource(
                    sourceValue,
                    &command->syncResult.adoptedOffsetSource) ||
            !FindStrictNsField(commandJson, "sync_avg_offset_ns", &command->syncResult.syncAvgOffsetNs) ||
            !FindStrictNsField(commandJson, "sync_avg_rtt_ns", &command->syncResult.syncAvgRttNs) ||
            !FindStrictNsField(commandJson, "verify_avg_offset_ns", &command->syncResult.verifyAvgOffsetNs) ||
            !FindStrictNsField(commandJson, "verify_avg_rtt_ns", &command->syncResult.verifyAvgRttNs) ||
            !FindStrictNsField(commandJson, "sync_verify_delta_ns", &command->syncResult.syncVerifyDeltaNs) ||
            !ParseRequiredIntField(commandJson, "sync_sample_count", &command->syncResult.syncSampleCount) ||
            !ParseRequiredIntField(commandJson,
                                   "sync_filtered_sample_count",
                                   &command->syncResult.syncFilteredSampleCount) ||
            !ParseRequiredIntField(commandJson,
                                   "verify_sample_count",
                                   &command->syncResult.verifySampleCount) ||
            !ParseRequiredIntField(commandJson,
                                   "verify_filtered_sample_count",
                                   &command->syncResult.verifyFilteredSampleCount)) {
            return false;
        }

        command->type = ControlCommandType::kSyncResult;
        command->syncResult.sessionId = command->sessionId;
        if (!ParseOptionalIntField(commandJson,
                                   "last_sample_index",
                                   &command->syncResult.hasLastSampleIndex,
                                   &command->syncResult.lastSampleIndex) ||
            !ParseOptionalNsField(commandJson,
                                  "last_t1_ns",
                                  &command->syncResult.hasLastT1Ns,
                                  &command->syncResult.lastT1Ns) ||
            !ParseOptionalNsField(commandJson,
                                  "last_t2_ns",
                                  &command->syncResult.hasLastT2Ns,
                                  &command->syncResult.lastT2Ns) ||
            !ParseOptionalNsField(commandJson,
                                  "last_t3_ns",
                                  &command->syncResult.hasLastT3Ns,
                                  &command->syncResult.lastT3Ns) ||
            !ParseOptionalNsField(commandJson,
                                  "last_t4_ns",
                                  &command->syncResult.hasLastT4Ns,
                                  &command->syncResult.lastT4Ns)) {
            return false;
        }
        return true;
    }

    if (type == "cancel_sync") {
        if (command->op != "sync" ||
            command->action != "cancel" ||
            !ParseRequiredSessionIdField(commandJson, &command->sessionId)) {
            return false;
        }
        command->type = ControlCommandType::kCancelSync;
        return true;
    }

    if (type == "get_time_sync_status") {
        if (command->op != "query" ||
            command->query != "status" ||
            !ParseRequiredSessionIdField(commandJson, &command->sessionId)) {
            return false;
        }
        command->type = ControlCommandType::kGetTimeSyncStatus;
        return true;
    }

    return false;
}

std::string BuildStartedJson(int sessionId) {
    std::ostringstream oss;
    oss << "{\"type\":\"time_sync_status\""
        << ",\"op\":\"sync\""
        << ",\"status\":\"started\""
        << ",\"session_id\":" << sessionId
        << "}";
    return oss.str();
}

std::string BuildReplyJson(const TimeSyncReply& reply) {
    std::ostringstream oss;
    oss << "{\"type\":\"time_sync_reply\""
        << ",\"op\":\"sync\""
        << ",\"status\":\"ok\"";
    AppendJsonStringField(oss, "phase", BleTimeSyncCore::PhaseName(reply.phase));
    oss << ",\"session_id\":" << reply.sessionId
        << ",\"sample_index\":" << reply.sampleIndex;
    AppendJsonInt64StringField(oss, "t1_ns", reply.t1Ns);
    AppendJsonInt64StringField(oss, "t2_ns", reply.t2Ns);
    AppendJsonInt64StringField(oss, "t3_ns", reply.t3Ns);
    oss << "}";
    return oss.str();
}

std::string BuildStatusJson(const TimeSyncStatus& status,
                            const std::string& op,
                            const std::string& statusValue,
                            ErrorCode lastError,
                            const std::string& lastErrorDetail) {
    std::ostringstream oss;
    oss << "{\"type\":\"time_sync_status\"";
    AppendJsonStringField(oss, "op", op.empty() ? "query" : op);
    AppendJsonStringField(oss,
                          "status",
                          statusValue.empty() ? QueryStatusName(status, lastError) : statusValue);
    AppendStatusFields(oss, status, lastError, lastErrorDetail);
    oss << "}";
    return oss.str();
}

std::string BuildErrorJson(ErrorCode error,
                           const std::string& op,
                           int sessionId,
                           const std::string& detail) {
    TimeSyncStatus status{};
    status.sessionId = sessionId;
    return BuildStatusJson(status,
                           op,
                           "failed",
                           error,
                           detail);
}

}  // namespace ble_time_sync
