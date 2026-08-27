#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "Core/Helpers.h"

namespace itch {

// Shared layout constants. Use only when this type actually has that field
// at this offset (e.g. Q has shares at 11, not order-ref; U has shares at 27).
struct Offsets {
    static constexpr size_t kMessageType = 0;
    static constexpr size_t kStockLocate = 1;
    static constexpr size_t kTrackingNumber = 3;
    static constexpr size_t kTimestamp = 5;
    static constexpr size_t kOrderReference = 11;  // A F E C X D U(original) P — not Q
    static constexpr size_t kBuySell = 19;         // A F P
    static constexpr size_t kShares = 20;          // A F E C X P — not U(27) or Q(11)
    static constexpr size_t kStock = 24;           // A F P — not Q(15)
    static constexpr size_t kPrice = 32;           // A F P — not U(31) or C exec(33)
};

class Message {
public:
    virtual ~Message() = default;

    // Parse payload bytes into this message's fields. Returns false if too short or wrong type.
    virtual bool decode(const std::vector<uint8_t>& data) = 0;

    [[nodiscard]] char message_type() const { return message_type_; }
    [[nodiscard]] uint16_t stock_locate() const { return stock_locate_; }
    [[nodiscard]] uint16_t tracking_number() const { return tracking_number_; }
    [[nodiscard]] uint64_t timestamp_ns() const { return timestamp_ns_; }

protected:
    // Every message shares the leading header through timestamp.
    bool decode_header(const std::vector<uint8_t>& data, size_t min_size, char expected_type);

    static uint16_t read_u16(const std::vector<uint8_t>& data, size_t offset);
    static uint32_t read_u32(const std::vector<uint8_t>& data, size_t offset);
    static uint64_t read_u64(const std::vector<uint8_t>& data, size_t offset);
    static uint64_t read_timestamp_ns(const std::vector<uint8_t>& data, size_t offset);

    char message_type_{};
    uint16_t stock_locate_{};
    uint16_t tracking_number_{};
    uint64_t timestamp_ns_{};
};

}  // namespace itch
