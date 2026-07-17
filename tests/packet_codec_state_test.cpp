#include "packet_codec.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Field {
    uint32_t number = 0;
    uint32_t wireType = 0;
    uint64_t value = 0;
    std::string bytes;
};

void AppendVarint(uint64_t value, std::string* out) {
    while (value >= 0x80U) {
        out->push_back(static_cast<char>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    out->push_back(static_cast<char>(value));
}

void AppendVarintField(uint32_t number, uint64_t value, std::string* out) {
    AppendVarint((static_cast<uint64_t>(number) << 3U), out);
    AppendVarint(value, out);
}

void AppendMessageField(uint32_t number,
                        const std::string& message,
                        std::string* out) {
    AppendVarint((static_cast<uint64_t>(number) << 3U) | 2U, out);
    AppendVarint(message.size(), out);
    out->append(message);
}

bool ReadVarint(const std::string& input, std::size_t* offset, uint64_t* value) {
    *value = 0;
    for (unsigned shift = 0; shift < 64U && *offset < input.size(); shift += 7U) {
        const uint8_t byte = static_cast<uint8_t>(input[(*offset)++]);
        *value |= static_cast<uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0U) {
            return true;
        }
    }
    return false;
}

bool ParseFields(const std::string& input, std::vector<Field>* fields) {
    std::size_t offset = 0;
    while (offset < input.size()) {
        uint64_t tag = 0;
        if (!ReadVarint(input, &offset, &tag)) {
            return false;
        }
        Field field;
        field.number = static_cast<uint32_t>(tag >> 3U);
        field.wireType = static_cast<uint32_t>(tag & 7U);
        if (field.wireType == 0U) {
            if (!ReadVarint(input, &offset, &field.value)) {
                return false;
            }
        } else if (field.wireType == 2U) {
            uint64_t size = 0;
            if (!ReadVarint(input, &offset, &size) ||
                size > input.size() - offset) {
                return false;
            }
            field.bytes.assign(input, offset, static_cast<std::size_t>(size));
            offset += static_cast<std::size_t>(size);
        } else if (field.wireType == 5U) {
            if (input.size() - offset < 4U) return false;
            offset += 4U;
        } else if (field.wireType == 1U) {
            if (input.size() - offset < 8U) return false;
            offset += 8U;
        } else {
            return false;
        }
        fields->push_back(field);
    }
    return true;
}

const Field* Find(const std::vector<Field>& fields, uint32_t number) {
    for (const Field& field : fields) {
        if (field.number == number) return &field;
    }
    return nullptr;
}

bool ExpectVarint(const std::vector<Field>& fields,
                  uint32_t number,
                  uint64_t expected) {
    const Field* field = Find(fields, number);
    if (field == nullptr || field->wireType != 0U || field->value != expected) {
        std::fprintf(stderr, "field %u: expected varint %llu\n", number,
                     static_cast<unsigned long long>(expected));
        return false;
    }
    return true;
}

std::string BuildCommandPacket(uint32_t commandValue) {
    std::string command;
    AppendVarintField(1, commandValue, &command);
    std::string packet;
    AppendVarintField(1, 77, &packet);
    AppendVarintField(2, 1234, &packet);
    AppendVarintField(3, egocollect::kProtocolVersion, &packet);
    AppendMessageField(10, command, &packet);
    return packet;
}

}  // namespace

int main() {
    static_assert(static_cast<uint32_t>(egocollect::CommandType::kStartCollect) == 1,
                  "CMD_START_COLLECT wire value changed");
    static_assert(static_cast<uint32_t>(egocollect::CommandType::kStopCollect) == 2,
                  "CMD_STOP_COLLECT wire value changed");
    static_assert(static_cast<uint32_t>(egocollect::CommandType::kStartVideo) == 3,
                  "CMD_START_VIDEO wire value changed");
    static_assert(static_cast<uint32_t>(egocollect::CommandType::kStopVideo) == 4,
                  "CMD_STOP_VIDEO wire value changed");

    egocollect::StatusMessage status;
    status.workingState = egocollect::WorkingState::kCollecting;
    status.operationMode = egocollect::OperationMode::kPhoneRecord;
    status.operationPhase = egocollect::OperationPhase::kStopping;
    status.stateRevision = 0x102030405ULL;

    std::string encoded;
    if (!egocollect::SerializeStatusPacket(9, 100, status, &encoded)) {
        std::fprintf(stderr, "SerializeStatusPacket failed\n");
        return 1;
    }

    std::vector<Field> packetFields;
    if (!ParseFields(encoded, &packetFields)) return 2;
    const Field* statusField = Find(packetFields, 20);
    if (statusField == nullptr || statusField->wireType != 2U) return 3;

    std::vector<Field> statusFields;
    if (!ParseFields(statusField->bytes, &statusFields)) return 4;
    const Field* deviceField = Find(statusFields, 3);
    if (deviceField == nullptr || deviceField->wireType != 2U) return 5;

    std::vector<Field> deviceFields;
    if (!ParseFields(deviceField->bytes, &deviceFields)) return 6;
    if (!ExpectVarint(deviceFields, 1, 1) ||
        !ExpectVarint(deviceFields, 2, 4) ||
        !ExpectVarint(deviceFields, 3, 2) ||
        !ExpectVarint(deviceFields, 4, 0x102030405ULL)) {
        return 7;
    }

    const egocollect::CommandType expected[] = {
            egocollect::CommandType::kStartCollect,
            egocollect::CommandType::kStopCollect,
            egocollect::CommandType::kStartVideo,
            egocollect::CommandType::kStopVideo};
    for (uint32_t value = 1; value <= 4; ++value) {
        egocollect::CommandPacket parsed;
        std::string error;
        if (!egocollect::ParseCommandPacket(BuildCommandPacket(value), &parsed, &error) ||
            parsed.command != expected[value - 1] || parsed.seq != 77 ||
            parsed.version != egocollect::kProtocolVersion) {
            std::fprintf(stderr, "command %u compatibility failed: %s\n",
                         value, error.c_str());
            return 8;
        }
    }

    std::puts("PASS packet codec DeviceState fields 1/2/3/4 and command values 1/2/3/4");
    return 0;
}
