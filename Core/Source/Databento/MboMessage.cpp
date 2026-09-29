#include "Databento/MboMessage.h"

namespace databento {

uint16_t MboMessage::read_u16_le(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t MboMessage::read_u32_le(const std::vector<uint8_t>& data, size_t offset)
{
    uint32_t result = 0;
    for (int i = 3; i >= 0; --i) {
        result = (result << 8) | static_cast<uint32_t>(data[offset + i]);
    }
    return result;
}

uint64_t MboMessage::read_u64_le(const std::vector<uint8_t>& data, size_t offset)
{
    uint64_t result = 0;
    for (int i = 7; i >= 0; --i) {
        result = (result << 8) | static_cast<uint64_t>(data[offset + i]);
    }
    return result;
}

int32_t MboMessage::read_i32_le(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<int32_t>(read_u32_le(data, offset));
}

int64_t MboMessage::read_i64_le(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<int64_t>(read_u64_le(data, offset));
}

bool MboMessage::decode_mbo_body(const std::vector<uint8_t>& data, Action expected_action)
{
    if (data.size() < kPayloadSize) {
        return false;
    }
    if (static_cast<Action>(data[kAction]) != expected_action) {
        return false;
    }

    instrument_id_ = read_u32_le(data, kInstrumentId);
    timestamp_ns_ = read_u64_le(data, kTsEvent);

    order_id_ = read_u64_le(data, kOrderId);
    price_ = read_i64_le(data, kPrice);
    size_ = read_u32_le(data, kSize);
    flags_ = data[kFlags];
    channel_id_ = data[kChannelId];
    action_ = expected_action;
    side_ = static_cast<Side>(data[kSide]);
    ts_recv_ = read_u64_le(data, kTsRecv);
    ts_in_delta_ = read_i32_le(data, kTsInDelta);
    sequence_ = read_u32_le(data, kSequence);

    message_type_ = static_cast<char>(expected_action);
    tracking_number_ = 0;

    return true;
}

bool MboMessage::decode(const std::vector<uint8_t>& data)
{
    if (data.size() < kAction + 1) {
        return false;
    }
    return decode_mbo_body(data, static_cast<Action>(data[kAction]));
}

}  // namespace databento
