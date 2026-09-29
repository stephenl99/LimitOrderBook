#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Itch/Message.h"

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

class MboMessage : public itch::Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] uint32_t instrument_id() const { return instrument_id_; }
    [[nodiscard]] uint64_t order_id() const { return order_id_; }
    [[nodiscard]] int64_t raw_price() const { return price_; }
    [[nodiscard]] uint32_t size() const { return size_; }
    [[nodiscard]] uint8_t flags() const { return flags_; }
    [[nodiscard]] uint8_t channel_id() const { return channel_id_; }
    [[nodiscard]] Action action() const { return action_; }
    [[nodiscard]] Side side() const { return side_; }
    [[nodiscard]] uint64_t ts_recv() const { return ts_recv_; }
    [[nodiscard]] int32_t ts_in_delta() const { return ts_in_delta_; }
    [[nodiscard]] uint32_t sequence() const { return sequence_; }

    static constexpr size_t kPayloadSize = 56;

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

protected:
    bool decode_mbo_body(const std::vector<uint8_t>& data, Action expected_action);

    static uint16_t read_u16_le(const std::vector<uint8_t>& data, size_t offset);
    static uint32_t read_u32_le(const std::vector<uint8_t>& data, size_t offset);
    static uint64_t read_u64_le(const std::vector<uint8_t>& data, size_t offset);
    static int32_t read_i32_le(const std::vector<uint8_t>& data, size_t offset);
    static int64_t read_i64_le(const std::vector<uint8_t>& data, size_t offset);

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
