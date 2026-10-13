#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Message/MarketMessage.h"

namespace mbo {

enum class Action : char {
    Modify = 'M',
    Trade = 'T',
    Fill = 'F',
    Cancel = 'C',
    Add = 'A',
    Clear = 'R',
    None = 'N',
};

enum class Side : char {
    Ask = 'A',
    Bid = 'B',
    None = 'N',
};

class MboMessage : public MarketMessage {
public:
    static std::optional<MboMessage> parse(std::span<const uint8_t> data);

    bool decode(std::span<const uint8_t> data) override;

    uint32_t instrument_id() const { return instrument_id_; }
    uint64_t order_id() const { return order_id_; }
    int64_t raw_price() const { return price_; }
    uint32_t size() const { return size_; }
    uint8_t flags() const { return flags_; }
    bool is_last() const { return (flags_ & kFlagLast) != 0; }
    bool is_snapshot() const { return (flags_ & kFlagSnapshot) != 0; }
    uint8_t channel_id() const { return channel_id_; }
    Action action() const { return action_; }
    Side side() const { return side_; }
    uint64_t ts_recv() const { return ts_recv_; }
    int32_t ts_in_delta() const { return ts_in_delta_; }
    uint32_t sequence() const { return sequence_; }

    static constexpr size_t kPayloadSize = 56;
    static constexpr uint8_t kFlagLast = 0x80;
    static constexpr uint8_t kFlagSnapshot = 0x20;

    static constexpr size_t kLength = 0;
    static constexpr size_t kRType = 1;
    static constexpr size_t kPublisherId = 2;
    static constexpr size_t kInstrumentId = 4;
    static constexpr size_t kTsEvent = 8;
    static constexpr size_t kOrderId = 16;
    static constexpr size_t kPrice = 24;
    static constexpr size_t kSize = 32;
    static constexpr size_t kFlags = 36;
    static constexpr size_t kChannelId = 37;
    static constexpr size_t kAction = 38;
    static constexpr size_t kSide = 39;
    static constexpr size_t kTsRecv = 40;
    static constexpr size_t kTsInDelta = 48;
    static constexpr size_t kSequence = 52;

private:
    static bool is_book_action(Action action);

    static uint16_t read_u16_le(std::span<const uint8_t> data, size_t offset);
    static uint32_t read_u32_le(std::span<const uint8_t> data, size_t offset);
    static uint64_t read_u64_le(std::span<const uint8_t> data, size_t offset);
    static int32_t read_i32_le(std::span<const uint8_t> data, size_t offset);
    static int64_t read_i64_le(std::span<const uint8_t> data, size_t offset);

    uint32_t instrument_id_{};
    uint64_t order_id_{};
    int64_t price_{};
    uint32_t size_{};
    uint8_t flags_{};
    uint8_t channel_id_{};
    Action action_{Action::None};
    Side side_{Side::None};
    uint64_t ts_recv_{};
    int32_t ts_in_delta_{};
    uint32_t sequence_{};
};

}  // namespace mbo
