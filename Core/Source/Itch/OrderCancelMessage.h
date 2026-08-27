#pragma once

#include <cstdint>
#include <vector>

#include "Itch/Message.h"

namespace itch {

class OrderCancelMessage : public Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] uint64_t order_reference_number() const { return order_reference_number_; }
    [[nodiscard]] uint32_t cancelled_shares() const { return cancelled_shares_; }

    static constexpr size_t kPayloadSize = 23;
    static constexpr size_t kCancelledShares = 19;  // same slot as E executed shares

private:
    uint64_t order_reference_number_{};
    uint32_t cancelled_shares_{};
};

}  // namespace itch
