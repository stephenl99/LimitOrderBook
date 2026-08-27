#include "Itch/Message.h"

namespace itch {

bool Message::decode_header(const std::vector<uint8_t>& data, size_t min_size, char expected_type)
{
    if (data.size() < min_size || data[Offsets::kMessageType] != expected_type) {
        return false;
    }
    message_type_ = static_cast<char>(data[Offsets::kMessageType]);
    stock_locate_ = read_u16(data, Offsets::kStockLocate);
    tracking_number_ = read_u16(data, Offsets::kTrackingNumber);
    timestamp_ns_ = read_timestamp_ns(data, Offsets::kTimestamp);
    return true;
}

uint16_t Message::read_u16(const std::vector<uint8_t>& data, size_t offset)
{
    return Helpers::convert<uint16_t, uint8_t>(
        const_cast<uint8_t*>(data.data() + offset), 2);
}

uint32_t Message::read_u32(const std::vector<uint8_t>& data, size_t offset)
{
    return Helpers::convert<uint32_t, uint8_t>(
        const_cast<uint8_t*>(data.data() + offset), 4);
}

uint64_t Message::read_u64(const std::vector<uint8_t>& data, size_t offset)
{
    return Helpers::convert<uint64_t, uint8_t>(
        const_cast<uint8_t*>(data.data() + offset), 8);
}

uint64_t Message::read_timestamp_ns(const std::vector<uint8_t>& data, size_t offset)
{
    return Helpers::convert<uint64_t, uint8_t>(
        const_cast<uint8_t*>(data.data() + offset), 6);
}

}  // namespace itch
