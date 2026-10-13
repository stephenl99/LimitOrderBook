#pragma once

#include <cstdint>
#include <vector>

#include "Message/Itch/Message.h"

namespace itch {

class OrderExecutedMessage : public Message {
public:
    bool decode(std::span<const uint8_t> data) override;

    uint64_t order_reference_number() const { return order_reference_number_; }
    uint32_t executed_shares() const { return executed_shares_; }
    uint64_t match_number() const { return match_number_; }

    static constexpr size_t kPayloadSize = 31;
    static constexpr size_t kExecutedShares = 19;  // E/X qty starts here (not Offsets::kShares=20)
    static constexpr size_t kMatchNumber = 23;

protected:
    uint64_t order_reference_number_{};
    uint32_t executed_shares_{};
    uint64_t match_number_{};
};

}  // namespace itch
