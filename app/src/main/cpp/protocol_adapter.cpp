#include "protocol_adapter.h"

#include "CameraEncoder.h"
#include "OperationCoordinator.h"
#include "NativeLogger.h"
#include "SdkStateBridge.h"
#include "packet_codec.h"
#include "platform_state_bridge.h"

#include <jni.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace protocol_adapter {
namespace {

constexpr char kLogTag[] = "ProtocolAdapter";
constexpr uint16_t kControlPort = 8801;
constexpr uint16_t kVideoPort = 8802;
constexpr int kAcceptPollTimeoutMs = 1000;
constexpr int kControlStatusIntervalMs = 1000;
constexpr int kFaultPollIntervalMs = 250;
constexpr float kOverheatTemperatureCelsius = 80.0f;
constexpr float kOverheatClearTemperatureCelsius = 78.0f;
constexpr std::size_t kControlReadBufferBytes = 4096;
constexpr std::size_t kMaxVideoQueueFrames = 24;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

extern "C" bool RequestDeviceReboot();

int64_t CurrentRealtimeMs() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000LL +
           static_cast<int64_t>(ts.tv_nsec) / 1000000LL;
}

int64_t CurrentRealtimeNs() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
           static_cast<int64_t>(ts.tv_nsec);
}

uint32_t ClampToUint32(int64_t value) {
    if (value <= 0) {
        return 0U;
    }
    if (value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(value);
}

bool SetSocketTimeouts(int fd, int timeoutMs) {
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

void CloseFd(int* fd) {
    if (fd != nullptr && *fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

bool SendAll(int fd, const std::string& bytes) {
    const char* data = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0U) {
        const ssize_t sent = send(fd, data, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool BuildStorageInfo(egocollect::StorageInfo* out) {
    if (out == nullptr) {
        return false;
    }
    out->hasValue = false;
    struct statvfs fs{};
    if (statvfs("/storage/emulated/0/Android/data/com.ssnwt.helloxr/files", &fs) != 0) {
        return false;
    }
    out->hasValue = true;
    out->totalBytes =
            static_cast<uint64_t>(fs.f_blocks) * static_cast<uint64_t>(fs.f_frsize);
    out->freeBytes =
            static_cast<uint64_t>(fs.f_bavail) * static_cast<uint64_t>(fs.f_frsize);
    return true;
}

bool ReadSkinTemperatureCelsius(float* outTemperature) {
    if (outTemperature == nullptr) {
        return false;
    }

    DIR* directory = opendir("/sys/class/thermal");
    if (directory == nullptr) {
        return false;
    }

    bool found = false;
    float maximumTemperature = 0.0f;
    for (dirent* entry = readdir(directory); entry != nullptr; entry = readdir(directory)) {
        const std::string zoneName(entry->d_name);
        if (zoneName.compare(0, std::strlen("thermal_zone"), "thermal_zone") != 0) {
            continue;
        }

        const std::string zonePath = "/sys/class/thermal/" + zoneName;
        std::ifstream typeFile(zonePath + "/type");
        std::string type;
        if (!(typeFile >> type)) {
            continue;
        }
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (type.find("skin") == std::string::npos) {
            continue;
        }

        std::ifstream temperatureFile(zonePath + "/temp");
        float rawTemperature = 0.0f;
        if (!(temperatureFile >> rawTemperature)) {
            continue;
        }
        const float temperature = rawTemperature > 1000.0f
                                          ? rawTemperature / 1000.0f
                                          : rawTemperature;
        if (temperature < 0.0f || temperature > 200.0f) {
            continue;
        }
        maximumTemperature = found ? std::max(maximumTemperature, temperature) : temperature;
        found = true;
    }
    closedir(directory);

    if (found) {
        *outTemperature = maximumTemperature;
    }
    return found;
}

bool ExtractNextEgFrame(std::string* buffer, std::string* payload) {
    if (buffer == nullptr || payload == nullptr) {
        return false;
    }

    while (buffer->size() >= 2U) {
        if ((*buffer)[0] == 'E' && (*buffer)[1] == 'G') {
            break;
        }
        const std::size_t nextMagic = buffer->find("EG", 1U);
        if (nextMagic == std::string::npos) {
            const char lastByte = buffer->back();
            buffer->assign(1, lastByte);
            return false;
        }
        buffer->erase(0, nextMagic);
    }

    if (buffer->size() < egocollect::kEgHeaderSize) {
        return false;
    }

    const uint32_t length =
            (static_cast<uint32_t>(static_cast<uint8_t>((*buffer)[2])) << 24U) |
            (static_cast<uint32_t>(static_cast<uint8_t>((*buffer)[3])) << 16U) |
            (static_cast<uint32_t>(static_cast<uint8_t>((*buffer)[4])) << 8U) |
            static_cast<uint32_t>(static_cast<uint8_t>((*buffer)[5]));
    if (length == 0U) {
        buffer->erase(0, egocollect::kEgHeaderSize);
        payload->clear();
        return true;
    }
    if (length > egocollect::kMaxFramePayloadBytes) {
        NATIVE_LOGW(kLogTag, "event=drop_frame reason=payload_too_large length=%u", length);
        buffer->erase(0, 2U);
        return false;
    }
    if (buffer->size() < egocollect::kEgHeaderSize + length) {
        return false;
    }

    payload->assign(buffer->data() + egocollect::kEgHeaderSize, length);
    buffer->erase(0, egocollect::kEgHeaderSize + length);
    return true;
}

void TrimLeadingControlWhitespace(std::string* buffer) {
    if (buffer == nullptr) {
        return;
    }
    std::size_t eraseCount = 0;
    while (eraseCount < buffer->size() &&
           std::isspace(static_cast<unsigned char>((*buffer)[eraseCount]))) {
        ++eraseCount;
    }
    if (eraseCount > 0U) {
        buffer->erase(0, eraseCount);
    }
}

bool ExtractNextJsonObject(std::string* buffer, std::string* json) {
    if (buffer == nullptr || json == nullptr || buffer->empty() || (*buffer)[0] != '{') {
        return false;
    }

    int depth = 0;
    bool inString = false;
    bool escaping = false;
    for (std::size_t i = 0; i < buffer->size(); ++i) {
        const char ch = (*buffer)[i];
        if (inString) {
            if (escaping) {
                escaping = false;
                continue;
            }
            if (ch == '\\') {
                escaping = true;
                continue;
            }
            if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0) {
                json->assign(buffer->data(), i + 1U);
                buffer->erase(0, i + 1U);
                return true;
            }
        }
    }
    return false;
}

class NtpSyncController {
public:
    enum class State {
        kStopped,
        kStarting,
        kRunning,
        kFailed,
    };

    struct Status {
        State state = State::kStopped;
        int64_t lastStartTimeMs = 0;
        int64_t lastStopTimeMs = 0;
        int64_t lastClientRequestUtcNs = 0;
        int64_t lastServerResponseUtcNs = 0;
        std::string lastError;
    };

    NtpSyncController() = default;
    ~NtpSyncController() = default;

    bool Start(std::string* error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state == State::kStarting || status_.state == State::kRunning) {
            if (error != nullptr) {
                *error = "ntp server already running";
            }
            return false;
        }
        status_.state = State::kStarting;
        status_.lastStartTimeMs = CurrentRealtimeMs();
        status_.lastError.clear();
        sessionActive_ = false;
        sessionId_ = 0U;
        sessionPhase_ = "idle";
        sessionStatus_ = "idle";
        lastSampleIndex_ = -1;
        nextSampleIndex_ = 0U;
        syncSampleCount_ = 0U;
        status_.state = State::kRunning;
        NATIVE_LOGI(kLogTag,
                    "event=custom_ntp_server_started role=server transport=tcp_control_json");
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = State::kStopped;
        status_.lastStopTimeMs = CurrentRealtimeMs();
        status_.lastError.clear();
        sessionActive_ = false;
        sessionId_ = 0U;
        sessionPhase_ = "idle";
        sessionStatus_ = "idle";
        lastSampleIndex_ = -1;
        nextSampleIndex_ = 0U;
        syncSampleCount_ = 0U;
    }

    Status GetStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    void OnClientDisconnected() {
        std::lock_guard<std::mutex> lock(mutex_);
        sessionActive_ = false;
        sessionId_ = 0U;
        sessionPhase_ = "idle";
        sessionStatus_ = "idle";
        lastSampleIndex_ = -1;
        nextSampleIndex_ = 0U;
        syncSampleCount_ = 0U;
    }

    bool HandleJsonMessage(const std::string& requestJson, std::string* responseJson) {
        if (responseJson == nullptr) {
            return false;
        }
        if (GetStatus().state != State::kRunning) {
            *responseJson =
                    BuildSessionStatusJson("query", "failed", "ntp_server_not_running");
            return true;
        }

        std::string type;
        std::string op;
        if (!FindJsonStringField(requestJson, "type", &type) ||
            !FindJsonStringField(requestJson, "op", &op)) {
            *responseJson = BuildSessionStatusJson("query", "failed", "malformed_command");
            return true;
        }

        if (type == "start_time_sync") {
            uint32_t sessionId = 0U;
            if (op != "sync" ||
                !FindJsonUint32Field(requestJson, "session_id", &sessionId)) {
                *responseJson = BuildSessionStatusJson("sync", "failed", "malformed_command");
                return true;
            }
            *responseJson = HandleStartSession(sessionId);
            return true;
        }

        if (type == "time_sync_request") {
            std::string phase;
            uint32_t sessionId = 0U;
            uint32_t sampleIndex = 0U;
            int64_t t1Ns = 0;
            if (op != "sync" ||
                !FindJsonStringField(requestJson, "phase", &phase) ||
                !FindJsonUint32Field(requestJson, "session_id", &sessionId) ||
                !FindJsonUint32Field(requestJson, "sample_index", &sampleIndex) ||
                !FindJsonInt64StringField(requestJson, "t1_ns", &t1Ns)) {
                *responseJson = BuildSessionStatusJson("sync", "failed", "malformed_command");
                return true;
            }
            *responseJson = HandleTimeSyncRequest(phase, sessionId, sampleIndex, t1Ns);
            return true;
        }

        if (type == "get_time_sync_status") {
            if (op != "query") {
                *responseJson = BuildSessionStatusJson("query", "failed", "malformed_command");
                return true;
            }
            *responseJson =
                    BuildSessionStatusJson("query", CurrentSessionStatus(), std::string());
            return true;
        }

        if (type == "cancel_sync") {
            uint32_t sessionId = 0U;
            if (op != "sync" ||
                !FindJsonUint32Field(requestJson, "session_id", &sessionId)) {
                *responseJson = BuildSessionStatusJson("sync", "failed", "malformed_command");
                return true;
            }
            *responseJson = HandleCancelSession(sessionId);
            return true;
        }

        *responseJson = BuildSessionStatusJson(op, "failed", "unsupported_command");
        return true;
    }

    std::map<std::string, std::string> BuildQueryStatusData() const {
        const Status status = GetStatus();
        std::map<std::string, std::string> data;
        data["scope"] = "time";
        data["op"] = "query";
        data["protocol"] = "custom_ntp";
        data["key"] = "ntp_status";
        data["state"] = StateName(status.state);
        data["role"] = "server";
        data["server_time_utc_ns"] = ToString(CurrentRealtimeNs());
        data["last_start_time_ms"] = ToString(status.lastStartTimeMs);
        data["last_stop_time_ms"] = ToString(status.lastStopTimeMs);
        data["last_client_request_utc_ns"] = ToString(status.lastClientRequestUtcNs);
        data["last_server_response_utc_ns"] = ToString(status.lastServerResponseUtcNs);
        if (status.state == State::kFailed && !status.lastError.empty()) {
            data["last_error"] = status.lastError;
        }
        return data;
    }

    std::map<std::string, std::string> BuildSetResponseData(const std::string& action) const {
        const Status status = GetStatus();
        std::map<std::string, std::string> data;
        data["scope"] = "time";
        data["op"] = "sync";
        data["protocol"] = "custom_ntp";
        data["action"] = action;
        data["role"] = "server";
        data["state"] = StateName(status.state);
        if (status.state == State::kFailed && !status.lastError.empty()) {
            data["last_error"] = status.lastError;
        }
        return data;
    }

private:
    static std::string EscapeJson(const std::string& value) {
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

    static bool SkipToJsonFieldValue(const std::string& json,
                                     const char* key,
                                     std::size_t* valuePos) {
        const std::string token = "\"" + std::string(key) + "\"";
        const std::size_t keyPos = json.find(token);
        if (keyPos == std::string::npos) {
            return false;
        }

        std::size_t pos = keyPos + token.size();
        while (pos < json.size() &&
               std::isspace(static_cast<unsigned char>(json[pos]))) {
            ++pos;
        }
        if (pos >= json.size() || json[pos] != ':') {
            return false;
        }
        ++pos;
        while (pos < json.size() &&
               std::isspace(static_cast<unsigned char>(json[pos]))) {
            ++pos;
        }
        if (pos >= json.size()) {
            return false;
        }

        *valuePos = pos;
        return true;
    }

    static bool FindJsonStringField(const std::string& json,
                                    const char* key,
                                    std::string* valueOut) {
        std::size_t pos = 0U;
        if (valueOut == nullptr ||
            !SkipToJsonFieldValue(json, key, &pos) ||
            json[pos] != '"') {
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

    static bool FindJsonUint32Field(const std::string& json,
                                    const char* key,
                                    uint32_t* valueOut) {
        int64_t value = 0;
        if (valueOut == nullptr || !FindJsonInt64Field(json, key, false, &value) ||
            value < 0 ||
            value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }
        *valueOut = static_cast<uint32_t>(value);
        return true;
    }

    static bool FindJsonInt64StringField(const std::string& json,
                                         const char* key,
                                         int64_t* valueOut) {
        return FindJsonInt64Field(json, key, true, valueOut);
    }

    static bool FindJsonInt64Field(const std::string& json,
                                   const char* key,
                                   bool requireQuoted,
                                   int64_t* valueOut) {
        std::size_t pos = 0U;
        if (valueOut == nullptr || !SkipToJsonFieldValue(json, key, &pos)) {
            return false;
        }

        bool quoted = false;
        if (json[pos] == '"') {
            quoted = true;
            ++pos;
        }
        if (requireQuoted && !quoted) {
            return false;
        }

        std::size_t end = pos;
        if (end < json.size() && (json[end] == '-' || json[end] == '+')) {
            ++end;
        }
        while (end < json.size() &&
               std::isdigit(static_cast<unsigned char>(json[end]))) {
            ++end;
        }
        if (end == pos) {
            return false;
        }
        if (quoted && (end >= json.size() || json[end] != '"')) {
            return false;
        }

        long long parsed = 0;
        const std::string number = json.substr(pos, end - pos);
        if (std::sscanf(number.c_str(), "%lld", &parsed) != 1) {
            return false;
        }
        *valueOut = static_cast<int64_t>(parsed);
        return true;
    }

    std::string HandleStartSession(uint32_t sessionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != State::kRunning) {
            return BuildSessionStatusJsonLocked("sync", "failed", "ntp_server_not_running");
        }
        sessionActive_ = true;
        sessionId_ = sessionId;
        sessionPhase_ = "sync";
        sessionStatus_ = "started";
        lastSampleIndex_ = -1;
        nextSampleIndex_ = 0U;
        syncSampleCount_ = 0U;
        return BuildSessionStatusJsonLocked("sync", "started", std::string());
    }

    std::string HandleCancelSession(uint32_t sessionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != State::kRunning) {
            return BuildSessionStatusJsonLocked("sync", "failed", "ntp_server_not_running");
        }
        if (sessionActive_ && sessionId_ != sessionId) {
            return BuildSessionStatusJsonLocked("sync", "failed", "mismatched_session");
        }
        sessionActive_ = false;
        sessionId_ = sessionId;
        sessionPhase_ = "idle";
        sessionStatus_ = "cancelled";
        lastSampleIndex_ = -1;
        nextSampleIndex_ = 0U;
        syncSampleCount_ = 0U;
        return BuildSessionStatusJsonLocked("sync", "cancelled", std::string());
    }

    std::string HandleTimeSyncRequest(const std::string& phase,
                                      uint32_t sessionId,
                                      uint32_t sampleIndex,
                                      int64_t t1Ns) {
        const int64_t t2Ns = CurrentRealtimeNs();
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != State::kRunning) {
            return BuildSessionStatusJsonLocked("sync", "failed", "ntp_server_not_running");
        }
        if (phase != "sync" && phase != "verify") {
            return BuildSessionStatusJsonLocked("sync", "failed", "malformed_command");
        }
        if (!sessionActive_) {
            return BuildSessionStatusJsonLocked("sync", "failed", "not_started");
        }
        if (sessionId_ != sessionId) {
            return BuildSessionStatusJsonLocked("sync", "failed", "mismatched_session");
        }

        const bool isLatestRetry =
                nextSampleIndex_ > 0U && sampleIndex == nextSampleIndex_ - 1U;
        if (phase == "sync") {
            if (sessionPhase_ != "sync") {
                return BuildSessionStatusJsonLocked("sync", "failed", "mismatched_phase");
            }
        } else if (sessionPhase_ == "sync") {
            if (syncSampleCount_ == 0U || sampleIndex != 0U) {
                return BuildSessionStatusJsonLocked("sync", "failed", "mismatched_phase");
            }
            sessionPhase_ = "verify";
            nextSampleIndex_ = 0U;
        } else if (sessionPhase_ != "verify") {
            return BuildSessionStatusJsonLocked("sync", "failed", "mismatched_phase");
        }
        const bool retryAfterPhaseTransition =
                nextSampleIndex_ > 0U && sampleIndex == nextSampleIndex_ - 1U;
        if (sampleIndex != nextSampleIndex_ && !isLatestRetry && !retryAfterPhaseTransition) {
            return BuildSessionStatusJsonLocked("sync", "failed", "mismatched_sample");
        }

        sessionPhase_ = phase;
        sessionStatus_ = phase;
        lastSampleIndex_ = static_cast<int>(sampleIndex);
        if (sampleIndex == nextSampleIndex_) {
            ++nextSampleIndex_;
            if (phase == "sync") {
                ++syncSampleCount_;
            }
        }
        status_.lastClientRequestUtcNs = t2Ns;
        const int64_t t3Ns = CurrentRealtimeNs();
        status_.lastServerResponseUtcNs = t3Ns;
        return BuildReplyJsonLocked(phase, sessionId, sampleIndex, t1Ns, t2Ns, t3Ns);
    }

    std::string CurrentSessionStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return CurrentSessionStatusLocked();
    }

    std::string CurrentSessionStatusLocked() const {
        if (status_.state == State::kFailed) {
            return "failed";
        }
        if (!sessionActive_) {
            return sessionStatus_.empty() ? "idle" : sessionStatus_;
        }
        return sessionStatus_.empty() ? sessionPhase_ : sessionStatus_;
    }

    std::string BuildSessionStatusJson(const std::string& op,
                                       const std::string& statusValue,
                                       const std::string& error) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return BuildSessionStatusJsonLocked(op, statusValue, error);
    }

    std::string BuildSessionStatusJsonLocked(const std::string& op,
                                             const std::string& statusValue,
                                             const std::string& error) const {
        std::ostringstream oss;
        oss << "{\"type\":\"time_sync_status\""
            << ",\"op\":\"" << EscapeJson(op.empty() ? "query" : op) << "\""
            << ",\"protocol\":\"custom_ntp\""
            << ",\"role\":\"server\""
            << ",\"state\":\"" << StateName(status_.state) << "\""
            << ",\"status\":\"" << EscapeJson(statusValue) << "\""
            << ",\"phase\":\"" << EscapeJson(sessionActive_ ? sessionPhase_ : "idle") << "\""
            << ",\"server_time_utc_ns\":\"" << ToString(CurrentRealtimeNs()) << "\""
            << ",\"last_client_request_utc_ns\":\""
            << ToString(status_.lastClientRequestUtcNs) << "\""
            << ",\"last_server_response_utc_ns\":\""
            << ToString(status_.lastServerResponseUtcNs) << "\"";
        if (sessionId_ != 0U) {
            oss << ",\"session_id\":" << sessionId_;
        }
        if (lastSampleIndex_ >= 0) {
            oss << ",\"last_sample_index\":" << lastSampleIndex_;
        }
        if (!error.empty()) {
            oss << ",\"last_error\":\"" << EscapeJson(error) << "\"";
        } else if (status_.state == State::kFailed && !status_.lastError.empty()) {
            oss << ",\"last_error\":\"" << EscapeJson(status_.lastError) << "\"";
        }
        oss << "}";
        return oss.str();
    }

    std::string BuildReplyJsonLocked(const std::string& phase,
                                     uint32_t sessionId,
                                     uint32_t sampleIndex,
                                     int64_t t1Ns,
                                     int64_t t2Ns,
                                     int64_t t3Ns) const {
        std::ostringstream oss;
        oss << "{\"type\":\"time_sync_reply\""
            << ",\"op\":\"sync\""
            << ",\"status\":\"ok\""
            << ",\"protocol\":\"custom_ntp\""
            << ",\"role\":\"server\""
            << ",\"clock_domain\":\"UTC\""
            << ",\"phase\":\"" << EscapeJson(phase) << "\""
            << ",\"session_id\":" << sessionId
            << ",\"sample_index\":" << sampleIndex
            << ",\"t1_ns\":\"" << ToString(t1Ns) << "\""
            << ",\"t2_ns\":\"" << ToString(t2Ns) << "\""
            << ",\"t3_ns\":\"" << ToString(t3Ns) << "\""
            << "}";
        return oss.str();
    }

    static std::string StateName(State state) {
        switch (state) {
            case State::kStopped:
                return "stopped";
            case State::kStarting:
                return "starting";
            case State::kRunning:
                return "running";
            case State::kFailed:
                return "failed";
        }
        return "unknown";
    }

    static std::string ToString(int64_t value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    static std::string ToString(int value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    mutable std::mutex mutex_;
    Status status_;
    bool sessionActive_ = false;
    uint32_t sessionId_ = 0U;
    std::string sessionPhase_ = "idle";
    std::string sessionStatus_ = "idle";
    int lastSampleIndex_ = -1;
    uint32_t nextSampleIndex_ = 0U;
    uint32_t syncSampleCount_ = 0U;
};

class ProtocolAdapterService final : public SXR::IEncoderOutputListener {
public:
    enum class StreamState { DISABLED, STARTING, STREAMING, STOPPING };
    explicit ProtocolAdapterService(const std::string& externalFilesDir)
        : externalFilesDir_(externalFilesDir) {}

    ~ProtocolAdapterService() override {
        Stop();
    }

    void Start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        SXR::CameraEncoder::setOutputListener(this);
        controlThread_ = std::thread(&ProtocolAdapterService::RunControlServer, this);
        statusThread_ = std::thread(&ProtocolAdapterService::RunStatusLoop, this);
        videoAcceptThread_ = std::thread(&ProtocolAdapterService::RunVideoServer, this);
        videoSendThread_ = std::thread(&ProtocolAdapterService::RunVideoSender, this);
    }

    void Stop() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }

        ntpController_.Stop();
        SXR::CameraEncoder::setOutputListener(nullptr);
        CloseFd(&controlListenFd_);
        CloseControlClient();
        CloseFd(&videoListenFd_);
        CloseVideoClient();
        videoQueueCv_.notify_all();

        if (controlThread_.joinable()) {
            controlThread_.join();
        }
        if (statusThread_.joinable()) {
            statusThread_.join();
        }
        if (videoAcceptThread_.joinable()) {
            videoAcceptThread_.join();
        }
        if (videoSendThread_.joinable()) {
            videoSendThread_.join();
        }
    }

    void OnRecordingSessionStarted() {
        std::lock_guard<std::mutex> lock(videoSessionMutex_);
        recordingSessionActive_ = true;
        activeVideoCodec_ = nullptr;
        rgbConfigAnnexB_.clear();
        replayConfigPending_ = videoRequested_.load() && GetVideoClientFd() >= 0;
        ClearVideoQueue();
    }

    void OnRgbEncoderReady(AMediaCodec* codec) {
        std::lock_guard<std::mutex> lock(videoSessionMutex_);
        if (!recordingSessionActive_) {
            return;
        }
        activeVideoCodec_ = codec;
        if (videoRequested_.load() && GetVideoClientFd() >= 0) {
            replayConfigPending_ = true;
        }
        MaybeReplayVideoConfigLocked();
    }

    void OnRecordingSessionStopped() {
        std::lock_guard<std::mutex> lock(videoSessionMutex_);
        recordingSessionActive_ = false;
        activeVideoCodec_ = nullptr;
        rgbConfigAnnexB_.clear();
        replayConfigPending_ = false;
        ClearVideoQueue();
    }

    void SetStreamingEnabled(bool enabled) {
        const StreamState current = streamState_.load();
        if ((enabled && current == StreamState::STREAMING) ||
            (!enabled && current == StreamState::DISABLED)) return;
        streamState_ = enabled ? StreamState::STARTING : StreamState::STOPPING;
        videoRequested_ = enabled;
        if (!enabled) {
            {
                std::lock_guard<std::mutex> lock(videoSessionMutex_);
                replayConfigPending_ = false;
            }
            CloseVideoClient();
            streamState_ = StreamState::DISABLED;
            return;
        }
        std::lock_guard<std::mutex> lock(videoSessionMutex_);
        if (recordingSessionActive_ && GetVideoClientFd() >= 0) {
            replayConfigPending_ = true;
            MaybeReplayVideoConfigLocked();
        }
        streamState_ = StreamState::STREAMING;
    }

    void NotifyAuthoritativeStateChanged() { SendStatusSnapshot(); }

    void onEncodedFrame(const char* group,
                        const uint8_t* data,
                        size_t size,
                        int64_t /*ptsUs*/,
                        bool isConfig) override {
        if (!running_.load() ||
            group == nullptr ||
            std::strcmp(group, "rgb") != 0 ||
            data == nullptr ||
            size == 0U) {
            return;
        }

        std::string annexB;
        if (!egocollect::TryConvertLengthPrefixedToAnnexB(data, size, &annexB)) {
            annexB.assign(reinterpret_cast<const char*>(data), size);
        }

        if (isConfig) {
            std::lock_guard<std::mutex> lock(videoSessionMutex_);
            if (!recordingSessionActive_) {
                return;
            }
            rgbConfigAnnexB_ = std::move(annexB);
            MaybeReplayVideoConfigLocked();
            return;
        }

        if (!videoRequested_.load()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(videoSessionMutex_);
            if (!recordingSessionActive_ || activeVideoCodec_ == nullptr) {
                return;
            }
        }

        if (GetVideoClientFd() < 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(videoQueueMutex_);
        if (videoQueue_.size() >= kMaxVideoQueueFrames) {
            videoQueue_.pop_front();
        }
        videoQueue_.push_back(std::move(annexB));
        videoQueueCv_.notify_one();
    }

private:
    struct FaultRecord {
        egocollect::FaultEvent event;
        bool clearsAutomatically = false;
        bool active = false;
    };

    uint64_t CurrentProtocolTimeMs() const {
        const int64_t utcMs = CurrentRealtimeMs();
        return utcMs > 0 ? static_cast<uint64_t>(utcMs) : 0U;
    }

    uint32_t NextSeq() {
        return nextOutboundSeq_.fetch_add(1U);
    }

    bool SetupTcpServer(uint16_t port, int* outFd) {
        if (outFd == nullptr) {
            return false;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            NATIVE_LOGE(kLogTag, "event=listen_failed port=%u errno=%d", port, errno);
            return false;
        }

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(fd, 1) != 0) {
            NATIVE_LOGE(kLogTag, "event=bind_failed port=%u errno=%d", port, errno);
            CloseFd(&fd);
            return false;
        }
        *outFd = fd;
        return true;
    }

    void RunControlServer() {
        if (!SetupTcpServer(kControlPort, &controlListenFd_)) {
            return;
        }
        NATIVE_LOGI(kLogTag, "event=control_listen port=%u", kControlPort);

        while (running_.load()) {
            sockaddr_in peer{};
            socklen_t peerLen = sizeof(peer);
            const int clientFd = accept(controlListenFd_,
                                        reinterpret_cast<sockaddr*>(&peer),
                                        &peerLen);
            if (clientFd < 0) {
                if (!running_.load() || errno == EINTR) {
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptPollTimeoutMs));
                continue;
            }

            SetSocketTimeouts(clientFd, 1000);
            CloseControlClient();
            {
                std::lock_guard<std::mutex> lock(controlMutex_);
                controlClientFd_ = clientFd;
            }
            lastStatusSentMs_ = 0;
            SendConnectionEvent(egocollect::ConnectionState::kConnected);
            SendStatusSnapshot();
            ReplayActiveFaults();
            HandleControlClient(clientFd);
            CloseControlClient();
            CloseVideoClient();
            videoRequested_ = false;
            std::lock_guard<std::mutex> lock(videoSessionMutex_);
            replayConfigPending_ = false;
        }
    }

    void HandleControlClient(int clientFd) {
        std::string rxBuffer;
        std::vector<char> readBuffer(kControlReadBufferBytes);
        while (running_.load()) {
            const ssize_t received = recv(clientFd, readBuffer.data(), readBuffer.size(), 0);
            if (received == 0) {
                NATIVE_LOGI(kLogTag, "event=control_client_closed");
                return;
            }
            if (received < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                NATIVE_LOGW(kLogTag, "event=control_recv_failed errno=%d", errno);
                return;
            }

            rxBuffer.append(readBuffer.data(), static_cast<std::size_t>(received));
            for (;;) {
                TrimLeadingControlWhitespace(&rxBuffer);
                if (rxBuffer.empty()) {
                    break;
                }

                if (rxBuffer[0] == 'E') {
                    if (rxBuffer.size() < 2U) {
                        break;
                    }
                    if (rxBuffer[1] != 'G') {
                        NATIVE_LOGW(kLogTag, "event=control_drop_invalid_prefix byte=%d",
                                    static_cast<int>(static_cast<unsigned char>(rxBuffer[0])));
                        rxBuffer.erase(0, 1U);
                        continue;
                    }

                    std::string payload;
                    if (!ExtractNextEgFrame(&rxBuffer, &payload)) {
                        break;
                    }
                    if (payload.empty()) {
                        continue;
                    }

                    egocollect::CommandPacket command;
                    std::string error;
                    if (!egocollect::ParseCommandPacket(payload, &command, &error)) {
                        NATIVE_LOGW(kLogTag,
                                    "event=command_parse_failed error=%s",
                                    error.c_str());
                        SendResponse(0U,
                                     egocollect::ResultCode::kInvalidParam,
                                     "invalid packet payload",
                                     std::map<std::string, std::string>());
                        continue;
                    }
                    DispatchCommand(command);
                    continue;
                }

                if (rxBuffer[0] == '{') {
                    std::string jsonMessage;
                    if (!ExtractNextJsonObject(&rxBuffer, &jsonMessage)) {
                        break;
                    }
                    HandleCustomNtpJsonMessage(jsonMessage);
                    continue;
                }

                std::size_t nextStart = rxBuffer.find("EG");
                const std::size_t nextJson = rxBuffer.find('{');
                if (nextJson != std::string::npos &&
                    (nextStart == std::string::npos || nextJson < nextStart)) {
                    nextStart = nextJson;
                }

                if (nextStart == std::string::npos) {
                    NATIVE_LOGW(kLogTag,
                                "event=control_drop_unrecognized_bytes count=%zu",
                                rxBuffer.size());
                    rxBuffer.clear();
                    break;
                }

                NATIVE_LOGW(kLogTag,
                            "event=control_resync skipped=%zu",
                            nextStart);
                rxBuffer.erase(0, nextStart);
            }
        }
    }

    void RunStatusLoop() {
        while (running_.load()) {
            MaybeUpdateFaults();

            const int fd = GetControlClientFd();
            if (fd >= 0) {
                const uint64_t nowMs = CurrentProtocolTimeMs();
                if (lastStatusSentMs_ == 0 ||
                    nowMs - lastStatusSentMs_ >= kControlStatusIntervalMs) {
                    SendStatusSnapshot();
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(kFaultPollIntervalMs));
        }
    }

    void RunVideoServer() {
        if (!SetupTcpServer(kVideoPort, &videoListenFd_)) {
            return;
        }
        NATIVE_LOGI(kLogTag, "event=video_listen port=%u", kVideoPort);

        while (running_.load()) {
            sockaddr_in peer{};
            socklen_t peerLen = sizeof(peer);
            const int clientFd = accept(videoListenFd_,
                                        reinterpret_cast<sockaddr*>(&peer),
                                        &peerLen);
            if (clientFd < 0) {
                if (!running_.load() || errno == EINTR) {
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptPollTimeoutMs));
                continue;
            }

            CloseVideoClient();
            {
                std::lock_guard<std::mutex> lock(videoMutex_);
                videoClientFd_ = clientFd;
            }
            NATIVE_LOGI(kLogTag, "event=video_client_connected port=%u", kVideoPort);
            std::lock_guard<std::mutex> lock(videoSessionMutex_);
            if (recordingSessionActive_ && videoRequested_.load()) {
                replayConfigPending_ = true;
                MaybeReplayVideoConfigLocked();
            }
        }
    }

    void RunVideoSender() {
        while (running_.load()) {
            std::string frame;
            {
                std::unique_lock<std::mutex> lock(videoQueueMutex_);
                videoQueueCv_.wait_for(lock,
                                       std::chrono::milliseconds(500),
                                       [this]() {
                                           return !running_.load() || !videoQueue_.empty();
                                       });
                if (!running_.load()) {
                    break;
                }
                if (videoQueue_.empty()) {
                    continue;
                }
                frame = std::move(videoQueue_.front());
                videoQueue_.pop_front();
            }

            int fd = -1;
            {
                std::lock_guard<std::mutex> lock(videoMutex_);
                fd = videoClientFd_;
            }
            if (fd < 0 || !videoRequested_.load()) {
                continue;
            }
            if (!SendAll(fd, frame)) {
                NATIVE_LOGW(kLogTag, "event=video_send_failed errno=%d", errno);
                CloseVideoClient();
                operation::Coordinator::Instance().HandleNetworkError("TCP 8802 send failure");
            }
        }
    }

    int GetControlClientFd() const {
        std::lock_guard<std::mutex> lock(controlMutex_);
        return controlClientFd_;
    }

    int GetVideoClientFd() {
        std::lock_guard<std::mutex> lock(videoMutex_);
        return videoClientFd_;
    }

    void CloseControlClient() {
        std::lock_guard<std::mutex> lock(controlMutex_);
        CloseFd(&controlClientFd_);
        ntpController_.OnClientDisconnected();
    }

    void ClearVideoQueue() {
        std::lock_guard<std::mutex> queueLock(videoQueueMutex_);
        videoQueue_.clear();
    }

    void CloseVideoClient() {
        {
            std::lock_guard<std::mutex> lock(videoMutex_);
            CloseFd(&videoClientFd_);
        }
        ClearVideoQueue();
    }

    void MaybeReplayVideoConfigLocked() {
        if (!replayConfigPending_ ||
            !recordingSessionActive_ ||
            !videoRequested_.load() ||
            activeVideoCodec_ == nullptr ||
            rgbConfigAnnexB_.empty() ||
            GetVideoClientFd() < 0) {
            return;
        }

        {
            std::lock_guard<std::mutex> queueLock(videoQueueMutex_);
            videoQueue_.clear();
            videoQueue_.push_back(rgbConfigAnnexB_);
        }
        replayConfigPending_ = false;
        videoQueueCv_.notify_one();
        SXR::CameraEncoder::requestKeyFrame(activeVideoCodec_, "rgb");
    }

    bool SendControlBytes(const std::string& bytes) {
        std::lock_guard<std::mutex> lock(controlMutex_);
        if (controlClientFd_ < 0) {
            return false;
        }
        if (!SendAll(controlClientFd_, bytes)) {
            NATIVE_LOGW(kLogTag, "event=control_send_failed errno=%d", errno);
            CloseFd(&controlClientFd_);
            return false;
        }
        return true;
    }

    bool SendPacketPayload(const std::string& packetPayload) {
        std::string frame;
        egocollect::PackEgFrame(packetPayload, &frame);
        return SendControlBytes(frame);
    }

    bool SendControlText(const std::string& text) {
        if (text.empty()) {
            return false;
        }
        std::string message = text;
        if (message.back() != '\n') {
            message.push_back('\n');
        }
        return SendControlBytes(message);
    }

    void SendResponse(uint32_t requestSeq,
                      egocollect::ResultCode code,
                      const std::string& message,
                      const std::map<std::string, std::string>& data) {
        egocollect::ResponseMessage response;
        response.requestSeq = requestSeq;
        response.code = code;
        response.message = message;
        response.data = data;

        std::string payload;
        if (egocollect::SerializeResponsePacket(NextSeq(),
                                                CurrentProtocolTimeMs(),
                                                response,
                                                &payload)) {
            SendPacketPayload(payload);
        }
    }

    void SendStatusSnapshot() {
        egocollect::StatusMessage status = BuildStatusSnapshot();
        std::string payload;
        if (egocollect::SerializeStatusPacket(NextSeq(),
                                              CurrentProtocolTimeMs(),
                                              status,
                                              &payload)) {
            if (SendPacketPayload(payload)) {
                lastStatusSentMs_ = CurrentProtocolTimeMs();
            }
        }
    }

    void SendFaultEvent(const egocollect::FaultEvent& fault) {
        egocollect::EventMessage event;
        event.kind = egocollect::EventMessage::Kind::kFault;
        event.fault = fault;
        std::string payload;
        if (egocollect::SerializeEventPacket(NextSeq(),
                                             CurrentProtocolTimeMs(),
                                             event,
                                             &payload)) {
            SendPacketPayload(payload);
        }
    }

    void SendFaultCleared(const std::string& code) {
        egocollect::EventMessage event;
        event.kind = egocollect::EventMessage::Kind::kFaultCleared;
        event.faultCleared.code = code;
        std::string payload;
        if (egocollect::SerializeEventPacket(NextSeq(),
                                             CurrentProtocolTimeMs(),
                                             event,
                                             &payload)) {
            SendPacketPayload(payload);
        }
    }

    void SendConnectionEvent(egocollect::ConnectionState state) {
        egocollect::EventMessage event;
        event.kind = egocollect::EventMessage::Kind::kConnectionChanged;
        event.connectionChanged.state = state;
        std::string payload;
        if (egocollect::SerializeEventPacket(NextSeq(),
                                             CurrentProtocolTimeMs(),
                                             event,
                                             &payload)) {
            SendPacketPayload(payload);
        }
    }

    void HandleCustomNtpJsonMessage(const std::string& jsonMessage) {
        std::string responseJson;
        if (!ntpController_.HandleJsonMessage(jsonMessage, &responseJson) ||
            responseJson.empty()) {
            return;
        }
        if (!SendControlText(responseJson)) {
            NATIVE_LOGW(kLogTag, "event=custom_ntp_response_send_failed");
        }
    }

    egocollect::StatusMessage BuildStatusSnapshot() {
        egocollect::StatusMessage status;
        sdk_state_bridge::StateSnapshot sdkState;
        const bool hasSdkState = sdk_state_bridge::ReadStateSnapshot(&sdkState);
        const platform_state_bridge::PlatformSnapshot platform =
                platform_state_bridge::GetPlatformSnapshot();

        if (platform.hasBattery && platform.batteryLevel >= 0) {
            status.battery.hasValue = true;
            status.battery.level = ClampToUint32(platform.batteryLevel);
            status.battery.charging = platform.batteryCharging;
            status.battery.voltage = platform.batteryVoltage;
            status.battery.hasTemperature = platform.hasBatteryTemperature;
            status.battery.temperature = platform.batteryTemperature;
        }

        if (platform.hasWifi) {
            status.wifi.hasValue = true;
            status.wifi.rssi = platform.wifiRssi;
            status.wifi.ssid = platform.wifiSsid;
            status.wifi.channel = platform.wifiChannel;
        }

        BuildStorageInfo(&status.storage);

        if (hasSdkState) {
            status.operationMode = static_cast<egocollect::OperationMode>(sdkState.operationMode);
            status.operationPhase = static_cast<egocollect::OperationPhase>(sdkState.operationPhase);
            status.stateRevision = sdkState.stateRevision;
        }
        const bool operationRecording = hasSdkState &&
                (sdkState.operationMode == 2U || sdkState.operationMode == 3U ||
                 sdkState.operationMode == 4U || sdkState.operationPhase == 2U);
        if (operationRecording) {
            status.workingState = egocollect::WorkingState::kCollecting;
        } else {
            status.workingState = egocollect::WorkingState::kIdle;
        }

        const bool cameraConnected = hasSdkState && sdkState.cameraContextAvailable;
        const bool cameraHealthy =
                cameraConnected &&
                sdkState.cameraRgbOpen &&
                sdkState.cameraTrackingOpen &&
                sdkState.cameraCtrlOpen &&
                sdkState.rgbFrameReady &&
                sdkState.trackingFrameReady &&
                sdkState.ctrlFrameReady;
        const bool imuConnected = hasSdkState && sdkState.imuRunning;
        const bool imuHealthy = imuConnected && !sdkState.imuFinished;
        const bool micConnected = hasSdkState && sdkState.micRunning;
        const bool micHealthy = micConnected && !sdkState.micFinished;

        status.peripherals.push_back(BuildPeripheral("camera", cameraConnected, cameraHealthy));
        status.peripherals.push_back(BuildPeripheral("imu", imuConnected, imuHealthy));
        status.peripherals.push_back(BuildPeripheral("mic", micConnected, micHealthy));

        return status;
    }

    egocollect::PeripheralState BuildPeripheral(const std::string& name,
                                                bool connected,
                                                bool healthy) const {
        egocollect::PeripheralState peripheral;
        peripheral.name = name;
        peripheral.connected = connected;
        peripheral.healthy = healthy;
        return peripheral;
    }

    bool HasAnyActiveFault() {
        std::lock_guard<std::mutex> lock(faultMutex_);
        for (std::map<std::string, FaultRecord>::const_iterator it = activeFaults_.begin();
             it != activeFaults_.end();
             ++it) {
            if (it->second.active) {
                return true;
            }
        }
        return false;
    }

    bool IsFaultActive(const std::string& code) {
        std::lock_guard<std::mutex> lock(faultMutex_);
        const std::map<std::string, FaultRecord>::const_iterator it = activeFaults_.find(code);
        return it != activeFaults_.end() && it->second.active;
    }

    egocollect::FaultEvent MakeFault(const std::string& code,
                                     const std::string& description,
                                     egocollect::FaultLevel level) const {
        egocollect::FaultEvent fault;
        fault.code = code;
        fault.description = description;
        fault.level = level;
        fault.raiseTimeMs = CurrentProtocolTimeMs();
        return fault;
    }

    void ReplayActiveFaults() {
        std::vector<egocollect::FaultEvent> faults;
        {
            std::lock_guard<std::mutex> lock(faultMutex_);
            for (std::map<std::string, FaultRecord>::const_iterator it = activeFaults_.begin();
                 it != activeFaults_.end();
                 ++it) {
                if (it->second.active) {
                    faults.push_back(it->second.event);
                }
            }
        }
        for (std::size_t i = 0; i < faults.size(); ++i) {
            SendFaultEvent(faults[i]);
        }
    }

    void ActivateFault(const egocollect::FaultEvent& fault, bool clearsAutomatically) {
        std::lock_guard<std::mutex> lock(faultMutex_);
        FaultRecord& record = activeFaults_[fault.code];
        if (record.active) {
            return;
        }
        record.event = fault;
        record.active = true;
        record.clearsAutomatically = clearsAutomatically;
        SendFaultEvent(fault);
    }

    void ClearFaultIfActive(const std::string& code) {
        std::lock_guard<std::mutex> lock(faultMutex_);
        std::map<std::string, FaultRecord>::iterator it = activeFaults_.find(code);
        if (it == activeFaults_.end() || !it->second.active || !it->second.clearsAutomatically) {
            return;
        }
        it->second.active = false;
        SendFaultCleared(code);
    }

    void MaybeUpdateFaults() {
        sdk_state_bridge::StateSnapshot sdkState;
        const bool hasSdkState = sdk_state_bridge::ReadStateSnapshot(&sdkState);
        const platform_state_bridge::PlatformSnapshot platform =
                platform_state_bridge::GetPlatformSnapshot();

        if (hasSdkState && sdkState.storageKnown && sdkState.storageLow) {
            ActivateFault(MakeFault("E_SD_FULL",
                                    "storage free space below SDK threshold",
                                    egocollect::FaultLevel::kError),
                          true);
        } else {
            ClearFaultIfActive("E_SD_FULL");
        }

        float skinTemperature = 0.0f;
        if (ReadSkinTemperatureCelsius(&skinTemperature)) {
            if (skinTemperature >= kOverheatTemperatureCelsius) {
                std::ostringstream description;
                description << "skin temperature " << skinTemperature
                            << " C reached " << kOverheatTemperatureCelsius
                            << " C threshold";
                ActivateFault(MakeFault("E_OVERHEAT",
                                        description.str(),
                                        egocollect::FaultLevel::kFatal),
                              true);
            } else if (skinTemperature < kOverheatClearTemperatureCelsius) {
                ClearFaultIfActive("E_OVERHEAT");
            }
        }

        if (platform.hasWifi && platform.wifiConnected) {
            wifiWasConnected_ = true;
            ClearFaultIfActive("E_WIFI_LOST");
            if (wifiDisconnectEventSent_) {
                SendConnectionEvent(egocollect::ConnectionState::kConnected);
                wifiDisconnectEventSent_ = false;
            }
        } else if (platform.hasWifi && wifiWasConnected_) {
            ActivateFault(MakeFault("E_WIFI_LOST",
                                    "wifi connection unavailable",
                                    egocollect::FaultLevel::kWarn),
                          true);
            if (!wifiDisconnectEventSent_) {
                SendConnectionEvent(egocollect::ConnectionState::kDisconnected);
                wifiDisconnectEventSent_ = true;
            }
        } else {
            ClearFaultIfActive("E_WIFI_LOST");
        }

        const sdk_state_bridge::ErrorSnapshot error = sdk_state_bridge::PeekError();
        if (error.hasPending && error.sequence != lastSdkErrorSequence_) {
            lastSdkErrorSequence_ = error.sequence;
            ActivateFault(MapSdkError(error), false);
        }
    }

    egocollect::FaultEvent MapSdkError(const sdk_state_bridge::ErrorSnapshot& error) const {
        if (error.code == "camera_init_failed") {
            return MakeFault("E_CAM_INIT_FAIL",
                             error.message.empty() ? "camera initialization failed"
                                                   : error.message,
                             egocollect::FaultLevel::kFatal);
        }
        if (error.code == "storage_low_start_blocked" ||
            error.code == "recording_auto_stopped_storage_low") {
            return MakeFault("E_SD_FULL",
                             error.message.empty() ? "storage full" : error.message,
                             egocollect::FaultLevel::kError);
        }
        return MakeFault("E_SYSTEM_FAULT",
                         error.message.empty() ? "internal SDK fault" : error.message,
                         egocollect::FaultLevel::kFatal);
    }

    void DispatchCommand(const egocollect::CommandPacket& command) {
        switch (command.command) {
            case egocollect::CommandType::kStartCollect:
                HandleStartCollect(command);
                break;
            case egocollect::CommandType::kStopCollect:
                HandleStopCollect(command);
                break;
            case egocollect::CommandType::kStartVideo:
                HandleStartVideo(command);
                break;
            case egocollect::CommandType::kStopVideo:
                HandleStopVideo(command);
                break;
            case egocollect::CommandType::kHeartbeat:
                HandleHeartbeat(command);
                break;
            case egocollect::CommandType::kGetParam:
                HandleGetParam(command);
                break;
            case egocollect::CommandType::kSetParam:
                HandleSetParam(command);
                break;
            case egocollect::CommandType::kReboot:
                HandleReboot(command);
                break;
            default:
                SendResponse(command.seq,
                             egocollect::ResultCode::kUnknownCommand,
                             "unsupported command",
                             std::map<std::string, std::string>());
                break;
        }
    }

    void HandleStartCollect(const egocollect::CommandPacket& command) {
        operation::Coordinator::Instance().HandleRecordStart(
                operation::RecordOrigin::PHONE, "phone CMD_START_COLLECT");
        SendResponse(command.seq,
                     egocollect::ResultCode::kOk,
                     "collect request accepted",
                     std::map<std::string, std::string>());
        SendStatusSnapshot();
    }

    void HandleStopCollect(const egocollect::CommandPacket& command) {
        operation::Coordinator::Instance().HandleRecordStop("phone CMD_STOP_COLLECT");
        SendResponse(command.seq,
                     egocollect::ResultCode::kOk,
                     "collect stopping",
                     std::map<std::string, std::string>());
        SendStatusSnapshot();
    }

    void HandleStartVideo(const egocollect::CommandPacket& command) {
        operation::Coordinator::Instance().HandlePreviewStart();
        std::map<std::string, std::string> data;
        data["port"] = "8802";
        data["stream_state"] = "active";
        data["stream_codec"] = "hevc";
        data["stream_format"] = "annex-b";
        SendResponse(command.seq,
                     egocollect::ResultCode::kOk,
                     "video stream enabled",
                     data);
        SendStatusSnapshot();
    }

    void HandleStopVideo(const egocollect::CommandPacket& command) {
        operation::Coordinator::Instance().HandlePreviewStop();
        SendResponse(command.seq,
                     egocollect::ResultCode::kOk,
                     "video stream disabled",
                     std::map<std::string, std::string>());
    }

    void HandleHeartbeat(const egocollect::CommandPacket& command) {
        lastHeartbeatMs_ = CurrentProtocolTimeMs();
        SendResponse(command.seq,
                     egocollect::ResultCode::kOk,
                     "heartbeat ok",
                     std::map<std::string, std::string>());
    }

    void HandleReboot(const egocollect::CommandPacket& command) {
        if (!RequestDeviceReboot()) {
            SendResponse(command.seq,
                         egocollect::ResultCode::kInternal,
                         "device reboot unavailable",
                         std::map<std::string, std::string>());
            return;
        }
        SendResponse(command.seq,
                     egocollect::ResultCode::kOk,
                     "device reboot scheduled",
                     std::map<std::string, std::string>());
    }

    void HandleGetParam(const egocollect::CommandPacket& command) {
        const std::string scope = GetParam(command.params, "scope");
        const std::string op = GetParam(command.params, "op");
        const std::string protocol = GetParam(command.params, "protocol");
        const std::string key = GetParam(command.params, "key");
        if (scope == "time" &&
            op == "query" &&
            protocol == "custom_ntp" &&
            key == "ntp_status") {
            SendResponse(command.seq,
                         egocollect::ResultCode::kOk,
                         "ntp status",
                         ntpController_.BuildQueryStatusData());
            return;
        }
        SendResponse(command.seq,
                     egocollect::ResultCode::kInvalidParam,
                     "unsupported custom_ntp get_param request",
                     std::map<std::string, std::string>());
    }

    void HandleSetParam(const egocollect::CommandPacket& command) {
        const std::string scope = GetParam(command.params, "scope");
        const std::string op = GetParam(command.params, "op");
        const std::string protocol = GetParam(command.params, "protocol");
        const std::string action = GetParam(command.params, "action");
        if (scope == "time" &&
            op == "sync" &&
            protocol == "custom_ntp" &&
            action == "start_ntp_server") {
            std::string error;
            if (!ntpController_.Start(&error)) {
                SendResponse(command.seq,
                             egocollect::ResultCode::kDeviceBusy,
                             error.empty() ? "ntp server busy" : error,
                             ntpController_.BuildSetResponseData(action));
                return;
            }
            SendResponse(command.seq,
                         egocollect::ResultCode::kOk,
                         "ntp server running",
                         ntpController_.BuildSetResponseData(action));
            return;
        }
        if (scope == "time" &&
            op == "sync" &&
            protocol == "custom_ntp" &&
            action == "stop_ntp_server") {
            ntpController_.Stop();
            SendResponse(command.seq,
                         egocollect::ResultCode::kOk,
                         "ntp server stopped",
                         ntpController_.BuildSetResponseData(action));
            return;
        }
        SendResponse(command.seq,
                     egocollect::ResultCode::kInvalidParam,
                     "unsupported custom_ntp set_param request",
                     std::map<std::string, std::string>());
    }

    static std::string GetParam(const std::map<std::string, std::string>& params,
                                const std::string& key) {
        const std::map<std::string, std::string>::const_iterator it = params.find(key);
        if (it == params.end()) {
            return std::string();
        }
        return it->second;
    }

    const std::string externalFilesDir_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> nextOutboundSeq_{1U};
    std::atomic<bool> videoRequested_{false};
    std::atomic<StreamState> streamState_{StreamState::DISABLED};
    std::atomic<uint64_t> lastHeartbeatMs_{0U};

    mutable std::mutex controlMutex_;
    int controlListenFd_ = -1;
    int controlClientFd_ = -1;
    std::thread controlThread_;

    std::mutex faultMutex_;
    std::map<std::string, FaultRecord> activeFaults_;
    uint64_t lastSdkErrorSequence_ = 0;
    bool wifiDisconnectEventSent_ = false;
    bool wifiWasConnected_ = false;

    int videoListenFd_ = -1;
    int videoClientFd_ = -1;
    std::mutex videoMutex_;
    std::mutex videoSessionMutex_;
    std::thread videoAcceptThread_;
    std::thread videoSendThread_;

    std::mutex videoQueueMutex_;
    std::condition_variable videoQueueCv_;
    std::deque<std::string> videoQueue_;
    AMediaCodec* activeVideoCodec_ = nullptr;
    std::string rgbConfigAnnexB_;
    bool recordingSessionActive_ = false;
    bool replayConfigPending_ = false;

    std::thread statusThread_;
    std::atomic<uint64_t> lastStatusSentMs_{0U};

    NtpSyncController ntpController_;
};

std::mutex& ServiceMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<ProtocolAdapterService>& ServiceInstance() {
    static std::unique_ptr<ProtocolAdapterService> service;
    return service;
}

}  // namespace

void Start(const std::string& externalFilesDir) {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance() != nullptr) {
        return;
    }
    ServiceInstance().reset(new ProtocolAdapterService(externalFilesDir));
    ServiceInstance()->Start();
}

void Stop() {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance() == nullptr) {
        return;
    }
    ServiceInstance()->Stop();
    ServiceInstance().reset();
}

void OnRecordingSessionStarted() {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance() == nullptr) {
        return;
    }
    ServiceInstance()->OnRecordingSessionStarted();
}

void OnRgbEncoderReady(AMediaCodec* codec) {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance() == nullptr) {
        return;
    }
    ServiceInstance()->OnRgbEncoderReady(codec);
}

void OnRecordingSessionStopped() {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance() == nullptr) {
        return;
    }
    ServiceInstance()->OnRecordingSessionStopped();
}

void SetStreamingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance()) ServiceInstance()->SetStreamingEnabled(enabled);
}

void NotifyAuthoritativeStateChanged() {
    std::lock_guard<std::mutex> lock(ServiceMutex());
    if (ServiceInstance()) ServiceInstance()->NotifyAuthoritativeStateChanged();
}

}  // namespace protocol_adapter
