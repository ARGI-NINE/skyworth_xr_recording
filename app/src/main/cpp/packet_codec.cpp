#include "packet_codec.h"

#include <algorithm>
#include <cstring>

namespace egocollect {
namespace {

enum class WireType : uint32_t {
    kVarint = 0,
    kFixed64 = 1,
    kLengthDelimited = 2,
    kFixed32 = 5,
};

void WriteVarint(uint64_t value, std::string* out) {
    while (value >= 0x80U) {
        out->push_back(static_cast<char>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out->push_back(static_cast<char>(value));
}

void WriteTag(uint32_t fieldNumber, WireType wireType, std::string* out) {
    WriteVarint((static_cast<uint64_t>(fieldNumber) << 3U) |
                    static_cast<uint64_t>(wireType),
                out);
}

void WriteBoolField(uint32_t fieldNumber, bool value, std::string* out) {
    WriteTag(fieldNumber, WireType::kVarint, out);
    WriteVarint(value ? 1U : 0U, out);
}

void WriteUInt32Field(uint32_t fieldNumber, uint32_t value, std::string* out) {
    WriteTag(fieldNumber, WireType::kVarint, out);
    WriteVarint(value, out);
}

void WriteUInt64Field(uint32_t fieldNumber, uint64_t value, std::string* out) {
    WriteTag(fieldNumber, WireType::kVarint, out);
    WriteVarint(value, out);
}

void WriteInt32Field(uint32_t fieldNumber, int32_t value, std::string* out) {
    WriteTag(fieldNumber, WireType::kVarint, out);
    WriteVarint(static_cast<uint64_t>(static_cast<int64_t>(value)), out);
}

void WriteStringField(uint32_t fieldNumber,
                      const std::string& value,
                      std::string* out) {
    WriteTag(fieldNumber, WireType::kLengthDelimited, out);
    WriteVarint(value.size(), out);
    out->append(value);
}

void WriteFixed32Field(uint32_t fieldNumber, uint32_t value, std::string* out) {
    WriteTag(fieldNumber, WireType::kFixed32, out);
    char bytes[4];
    bytes[0] = static_cast<char>(value & 0xFFU);
    bytes[1] = static_cast<char>((value >> 8U) & 0xFFU);
    bytes[2] = static_cast<char>((value >> 16U) & 0xFFU);
    bytes[3] = static_cast<char>((value >> 24U) & 0xFFU);
    out->append(bytes, sizeof(bytes));
}

void WriteFloatField(uint32_t fieldNumber, float value, std::string* out) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteFixed32Field(fieldNumber, bits, out);
}

void WriteMessageField(uint32_t fieldNumber,
                       const std::string& message,
                       std::string* out) {
    WriteTag(fieldNumber, WireType::kLengthDelimited, out);
    WriteVarint(message.size(), out);
    out->append(message);
}

void WriteMapEntry(const std::string& key,
                   const std::string& value,
                   std::string* out) {
    std::string entry;
    WriteStringField(1, key, &entry);
    WriteStringField(2, value, &entry);
    WriteMessageField(10, entry, out);
}

bool ReadVarint(const std::string& data, std::size_t* offset, uint64_t* out) {
    uint64_t value = 0;
    uint32_t shift = 0;
    while (*offset < data.size() && shift < 64U) {
        const uint8_t byte = static_cast<uint8_t>(data[*offset]);
        *offset += 1U;
        value |= static_cast<uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U) {
            *out = value;
            return true;
        }
        shift += 7U;
    }
    return false;
}

bool ReadLengthDelimited(const std::string& data,
                         std::size_t* offset,
                         std::string* out) {
    uint64_t length = 0;
    if (!ReadVarint(data, offset, &length)) {
        return false;
    }
    if (length > data.size() - *offset) {
        return false;
    }
    out->assign(data.data() + *offset, static_cast<std::size_t>(length));
    *offset += static_cast<std::size_t>(length);
    return true;
}

bool SkipField(WireType wireType, const std::string& data, std::size_t* offset) {
    switch (wireType) {
        case WireType::kVarint: {
            uint64_t ignored = 0;
            return ReadVarint(data, offset, &ignored);
        }
        case WireType::kFixed64:
            if (data.size() - *offset < 8U) {
                return false;
            }
            *offset += 8U;
            return true;
        case WireType::kLengthDelimited: {
            std::string ignored;
            return ReadLengthDelimited(data, offset, &ignored);
        }
        case WireType::kFixed32:
            if (data.size() - *offset < 4U) {
                return false;
            }
            *offset += 4U;
            return true;
        default:
            return false;
    }
}

bool ParseMapEntry(const std::string& data,
                   std::map<std::string, std::string>* out) {
    std::string key;
    std::string value;
    std::size_t offset = 0;
    while (offset < data.size()) {
        uint64_t rawTag = 0;
        if (!ReadVarint(data, &offset, &rawTag)) {
            return false;
        }
        const uint32_t fieldNumber = static_cast<uint32_t>(rawTag >> 3U);
        const WireType wireType = static_cast<WireType>(rawTag & 0x07U);
        if (fieldNumber == 1 && wireType == WireType::kLengthDelimited) {
            if (!ReadLengthDelimited(data, &offset, &key)) {
                return false;
            }
        } else if (fieldNumber == 2 && wireType == WireType::kLengthDelimited) {
            if (!ReadLengthDelimited(data, &offset, &value)) {
                return false;
            }
        } else if (!SkipField(wireType, data, &offset)) {
            return false;
        }
    }
    if (!key.empty()) {
        (*out)[key] = value;
    }
    return true;
}

bool ParseCommandMessage(const std::string& data,
                         CommandPacket* out,
                         std::string* error) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        uint64_t rawTag = 0;
        if (!ReadVarint(data, &offset, &rawTag)) {
            if (error != nullptr) {
                *error = "failed to parse command tag";
            }
            return false;
        }
        const uint32_t fieldNumber = static_cast<uint32_t>(rawTag >> 3U);
        const WireType wireType = static_cast<WireType>(rawTag & 0x07U);
        if (fieldNumber == 1 && wireType == WireType::kVarint) {
            uint64_t value = 0;
            if (!ReadVarint(data, &offset, &value)) {
                if (error != nullptr) {
                    *error = "failed to parse command enum";
                }
                return false;
            }
            const CommandType decoded = static_cast<CommandType>(value);
            out->command = IsSupportedCommandType(decoded) ? decoded
                                                           : CommandType::kUnknown;
        } else if (fieldNumber == 10 && wireType == WireType::kLengthDelimited) {
            std::string entry;
            if (!ReadLengthDelimited(data, &offset, &entry) ||
                !ParseMapEntry(entry, &out->params)) {
                if (error != nullptr) {
                    *error = "failed to parse command params";
                }
                return false;
            }
        } else if (!SkipField(wireType, data, &offset)) {
            if (error != nullptr) {
                *error = "failed to skip unknown command field";
            }
            return false;
        }
    }
    return true;
}

bool SerializePacketEnvelope(uint32_t seq,
                             uint64_t timestampMs,
                             uint32_t payloadFieldNumber,
                             const std::string& payload,
                             std::string* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    WriteUInt32Field(1, seq, out);
    WriteUInt64Field(2, timestampMs, out);
    WriteUInt32Field(3, kProtocolVersion, out);
    WriteMessageField(payloadFieldNumber, payload, out);
    return true;
}

std::string BuildBatteryMessage(const BatteryInfo& battery) {
    std::string out;
    if (!battery.hasValue) {
        return out;
    }
    WriteUInt32Field(1, battery.level, &out);
    WriteBoolField(2, battery.charging, &out);
    WriteFloatField(3, battery.voltage, &out);
    if (battery.hasTemperature) {
        WriteFloatField(4, battery.temperature, &out);
    }
    return out;
}

std::string BuildWifiMessage(const WifiInfo& wifi) {
    std::string out;
    if (!wifi.hasValue) {
        return out;
    }
    WriteInt32Field(1, wifi.rssi, &out);
    if (!wifi.ssid.empty()) {
        WriteStringField(2, wifi.ssid, &out);
    }
    if (wifi.channel != 0U) {
        WriteUInt32Field(3, wifi.channel, &out);
    }
    return out;
}

std::string BuildDeviceStateMessage(const StatusMessage& status) {
    std::string out;
    WriteUInt32Field(2, static_cast<uint32_t>(status.operationMode), &out);
    WriteUInt32Field(3, static_cast<uint32_t>(status.operationPhase), &out);
    WriteUInt64Field(4, status.stateRevision, &out);
    return out;
}

std::string BuildStorageMessage(const StorageInfo& storage) {
    std::string out;
    if (!storage.hasValue) {
        return out;
    }
    WriteUInt64Field(1, storage.totalBytes, &out);
    WriteUInt64Field(2, storage.freeBytes, &out);
    return out;
}

std::string BuildPeripheralMessage(const PeripheralState& peripheral) {
    std::string out;
    WriteStringField(1, peripheral.name, &out);
    WriteBoolField(2, peripheral.connected, &out);
    WriteBoolField(3, peripheral.healthy, &out);
    return out;
}

std::string BuildStatusMessage(const StatusMessage& status) {
    std::string out;
    const std::string battery = BuildBatteryMessage(status.battery);
    if (!battery.empty()) {
        WriteMessageField(1, battery, &out);
    }
    const std::string wifi = BuildWifiMessage(status.wifi);
    if (!wifi.empty()) {
        WriteMessageField(2, wifi, &out);
    }
    WriteMessageField(3, BuildDeviceStateMessage(status), &out);
    const std::string storage = BuildStorageMessage(status.storage);
    if (!storage.empty()) {
        WriteMessageField(4, storage, &out);
    }
    for (std::size_t i = 0; i < status.peripherals.size(); ++i) {
        WriteMessageField(10, BuildPeripheralMessage(status.peripherals[i]), &out);
    }
    return out;
}

std::string BuildResponseMessage(const ResponseMessage& response) {
    std::string out;
    WriteUInt32Field(1, response.requestSeq, &out);
    const ResultCode wireCode =
            IsSupportedResultCode(response.code) ? response.code
                                                 : ResultCode::kInternal;
    WriteUInt32Field(2, static_cast<uint32_t>(wireCode), &out);
    if (!response.message.empty()) {
        WriteStringField(3, response.message, &out);
    }
    for (std::map<std::string, std::string>::const_iterator it = response.data.begin();
         it != response.data.end();
         ++it) {
        WriteMapEntry(it->first, it->second, &out);
    }
    return out;
}

std::string BuildFaultMessage(const FaultEvent& fault) {
    std::string out;
    WriteStringField(1, fault.code, &out);
    if (!fault.description.empty()) {
        WriteStringField(2, fault.description, &out);
    }
    WriteUInt32Field(3, static_cast<uint32_t>(fault.level), &out);
    WriteUInt64Field(4, fault.raiseTimeMs, &out);
    return out;
}

std::string BuildFaultClearedMessage(const FaultClearedEvent& cleared) {
    std::string out;
    WriteStringField(1, cleared.code, &out);
    return out;
}

std::string BuildConnectionChangedMessage(const ConnectionChangedEvent& connection) {
    std::string out;
    WriteUInt32Field(1, static_cast<uint32_t>(connection.state), &out);
    return out;
}

std::string BuildEventMessage(const EventMessage& event) {
    std::string out;
    switch (event.kind) {
        case EventMessage::Kind::kFault:
            WriteMessageField(1, BuildFaultMessage(event.fault), &out);
            break;
        case EventMessage::Kind::kFaultCleared:
            WriteMessageField(2, BuildFaultClearedMessage(event.faultCleared), &out);
            break;
        case EventMessage::Kind::kConnectionChanged:
            WriteMessageField(3, BuildConnectionChangedMessage(event.connectionChanged), &out);
            break;
        case EventMessage::Kind::kNone:
        default:
            break;
    }
    return out;
}

}  // namespace

bool ParseCommandPacket(const std::string& payload,
                        CommandPacket* out,
                        std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null command output";
        }
        return false;
    }

    *out = CommandPacket{};
    std::size_t offset = 0;
    bool hasCommand = false;
    while (offset < payload.size()) {
        uint64_t rawTag = 0;
        if (!ReadVarint(payload, &offset, &rawTag)) {
            if (error != nullptr) {
                *error = "failed to parse packet tag";
            }
            return false;
        }
        const uint32_t fieldNumber = static_cast<uint32_t>(rawTag >> 3U);
        const WireType wireType = static_cast<WireType>(rawTag & 0x07U);
        if (fieldNumber == 1 && wireType == WireType::kVarint) {
            uint64_t value = 0;
            if (!ReadVarint(payload, &offset, &value)) {
                if (error != nullptr) {
                    *error = "failed to parse seq";
                }
                return false;
            }
            out->seq = static_cast<uint32_t>(value);
        } else if (fieldNumber == 2 && wireType == WireType::kVarint) {
            uint64_t value = 0;
            if (!ReadVarint(payload, &offset, &value)) {
                if (error != nullptr) {
                    *error = "failed to parse timestamp";
                }
                return false;
            }
            out->timestampMs = value;
        } else if (fieldNumber == 3 && wireType == WireType::kVarint) {
            uint64_t value = 0;
            if (!ReadVarint(payload, &offset, &value)) {
                if (error != nullptr) {
                    *error = "failed to parse version";
                }
                return false;
            }
            out->version = static_cast<uint32_t>(value);
        } else if (fieldNumber == 10 && wireType == WireType::kLengthDelimited) {
            std::string commandMessage;
            if (!ReadLengthDelimited(payload, &offset, &commandMessage) ||
                !ParseCommandMessage(commandMessage, out, error)) {
                return false;
            }
            hasCommand = true;
        } else if (!SkipField(wireType, payload, &offset)) {
            if (error != nullptr) {
                *error = "failed to skip unknown packet field";
            }
            return false;
        }
    }

    if (!hasCommand) {
        if (error != nullptr) {
            *error = "packet does not contain command payload";
        }
        return false;
    }
    return true;
}

bool SerializeStatusPacket(uint32_t seq,
                           uint64_t timestampMs,
                           const StatusMessage& status,
                           std::string* out) {
    return SerializePacketEnvelope(seq,
                                   timestampMs,
                                   20,
                                   BuildStatusMessage(status),
                                   out);
}

bool SerializeResponsePacket(uint32_t seq,
                             uint64_t timestampMs,
                             const ResponseMessage& response,
                             std::string* out) {
    return SerializePacketEnvelope(seq,
                                   timestampMs,
                                   30,
                                   BuildResponseMessage(response),
                                   out);
}

bool SerializeEventPacket(uint32_t seq,
                          uint64_t timestampMs,
                          const EventMessage& event,
                          std::string* out) {
    return SerializePacketEnvelope(seq,
                                   timestampMs,
                                   40,
                                   BuildEventMessage(event),
                                   out);
}

void PackEgFrame(const std::string& payload, std::string* out) {
    if (out == nullptr) {
        return;
    }
    out->clear();
    out->reserve(kEgHeaderSize + payload.size());
    out->push_back('E');
    out->push_back('G');
    const uint32_t payloadSize = static_cast<uint32_t>(payload.size());
    out->push_back(static_cast<char>((payloadSize >> 24U) & 0xFFU));
    out->push_back(static_cast<char>((payloadSize >> 16U) & 0xFFU));
    out->push_back(static_cast<char>((payloadSize >> 8U) & 0xFFU));
    out->push_back(static_cast<char>(payloadSize & 0xFFU));
    out->append(payload);
}

bool TryConvertLengthPrefixedToAnnexB(const uint8_t* data,
                                      std::size_t size,
                                      std::string* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (data == nullptr || size == 0U) {
        return false;
    }

    if (size >= 4U &&
        data[0] == 0x00U &&
        data[1] == 0x00U &&
        ((data[2] == 0x01U) || (data[2] == 0x00U && data[3] == 0x01U))) {
        out->assign(reinterpret_cast<const char*>(data), size);
        return true;
    }

    std::size_t offset = 0;
    while (offset + 4U <= size) {
        const uint32_t nalSize =
                (static_cast<uint32_t>(data[offset]) << 24U) |
                (static_cast<uint32_t>(data[offset + 1U]) << 16U) |
                (static_cast<uint32_t>(data[offset + 2U]) << 8U) |
                static_cast<uint32_t>(data[offset + 3U]);
        offset += 4U;
        if (nalSize == 0U || nalSize > size - offset) {
            out->clear();
            return false;
        }
        static const char kStartCode[] = "\x00\x00\x00\x01";
        out->append(kStartCode, 4);
        out->append(reinterpret_cast<const char*>(data + offset), nalSize);
        offset += nalSize;
    }

    if (offset != size) {
        out->clear();
        return false;
    }
    return !out->empty();
}

}  // namespace egocollect
