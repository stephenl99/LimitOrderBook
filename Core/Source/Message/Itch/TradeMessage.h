#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Message/Itch/Message.h"

namespace itch {

class TradeMessage : public Message {
public:
    bool decode(std::span<const uint8_t> data) override;

    uint64_t order_reference_number() const { return order_reference_number_; }
    char side() const { return side_; }
    uint32_t shares() const { return shares_; }
    const char* stock() const { return stock_.data(); }
    uint32_t price() const { return price_; }
    uint64_t match_number() const { return match_number_; }

    static constexpr size_t kPayloadSize = 44;
    static constexpr size_t kMatchNumber = 36;

private:
    uint64_t order_reference_number_{};
    char side_{};
    uint32_t shares_{};
    std::array<char, 8> stock_{};
    uint32_t price_{};
    uint64_t match_number_{};
};

}  // namespace itch
