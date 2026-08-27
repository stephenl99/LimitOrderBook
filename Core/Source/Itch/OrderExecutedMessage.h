#pragma once

#include <cstdint>
#include <vector>

#include "Itch/Message.h"

namespace itch {

class OrderExecutedMessage : public Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] uint64_t order_reference_number() const { return order_reference_number_; }
    [[nodiscard]] uint32_t executed_shares() const { return executed_shares_; }
    [[nodiscard]] uint64_t match_number() const { return match_number_; }

    static constexpr size_t kPayloadSize = 31;
    static constexpr size_t kMatchNumber = 24;  // E/C-specific; shares use Offsets::kShares

protected:
    uint64_t order_reference_number_{};
    uint32_t executed_shares_{};
    uint64_t match_number_{};
};

}  // namespace itch
