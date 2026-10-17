#include "Message/Itch/Message.h"

namespace {

template <typename R>
R read_big_endian(std::span<const uint8_t> data, size_t offset, size_t size)
{
    R value = 0;
    for (size_t i = 0; i < size; ++i) {
        value = (value << 8) | static_cast<R>(data[offset + i]);
    }
    return value;
}

}  // namespace

namespace itch {

bool Message::decode_header(std::span<const uint8_t> data, size_t min_size, char expected_type)
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

uint16_t Message::read_u16(std::span<const uint8_t> data, size_t offset)
{
    return read_big_endian<uint16_t>(data, offset, 2);
}

uint32_t Message::read_u32(std::span<const uint8_t> data, size_t offset)
{
    return read_big_endian<uint32_t>(data, offset, 4);
}

uint64_t Message::read_u64(std::span<const uint8_t> data, size_t offset)
{
    return read_big_endian<uint64_t>(data, offset, 8);
}

uint64_t Message::read_timestamp_ns(std::span<const uint8_t> data, size_t offset)
{
    return read_big_endian<uint64_t>(data, offset, 6);
}

}  // namespace itch
