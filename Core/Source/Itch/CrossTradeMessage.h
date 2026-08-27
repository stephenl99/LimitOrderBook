#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Itch/Message.h"

namespace itch {

class CrossTradeMessage : public Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] uint32_t shares() const { return shares_; }
    [[nodiscard]] const char* stock() const { return stock_.data(); }
    [[nodiscard]] uint32_t cross_price() const { return cross_price_; }
    [[nodiscard]] uint64_t match_number() const { return match_number_; }
    [[nodiscard]] char cross_type() const { return cross_type_; }

    static constexpr size_t kPayloadSize = 40;
    static constexpr size_t kShares = 11;  // Q-specific (not Offsets::kShares / not order-ref)
    static constexpr size_t kStock = 15;
    static constexpr size_t kCrossPrice = 23;
    static constexpr size_t kMatchNumber = 27;
    static constexpr size_t kCrossType = 35;

private:
    uint32_t shares_{};
    std::array<char, 8> stock_{};
    uint32_t cross_price_{};
    uint64_t match_number_{};
    char cross_type_{};
};

}  // namespace itch
