#pragma once

#include <cstdint>
#include <vector>

#include "Message/Itch/Message.h"

namespace itch {

class OrderReplaceMessage : public Message {
public:
    bool decode(std::span<const uint8_t> data) override;

    uint64_t original_order_reference() const { return original_order_reference_; }
    uint64_t new_order_reference() const { return new_order_reference_; }
    uint32_t shares() const { return shares_; }
    uint32_t price() const { return price_; }

    static constexpr size_t kPayloadSize = 35;
    static constexpr size_t kNewOrderReference = 19;
    static constexpr size_t kShares = 27;  // U-specific (not Offsets::kShares)
    static constexpr size_t kPrice = 31;   // U-specific (not Offsets::kPrice)

private:
    uint64_t original_order_reference_{};
    uint64_t new_order_reference_{};
    uint32_t shares_{};
    uint32_t price_{};
};

}  // namespace itch
