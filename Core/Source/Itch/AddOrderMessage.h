#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Itch/Message.h"

namespace itch {

class AddOrderMessage : public Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] uint64_t order_reference_number() const { return order_reference_number_; }
    [[nodiscard]] char side() const { return side_; }
    [[nodiscard]] uint32_t shares() const { return shares_; }
    [[nodiscard]] const char* stock() const { return stock_.data(); }
    [[nodiscard]] uint32_t price() const { return price_; }

    static constexpr size_t kPayloadSize = 36;
    // Same values as Offsets::* for A/F — listed here so the Add layout is obvious.
    static constexpr size_t kOrderReference = Offsets::kOrderReference;
    static constexpr size_t kBuySell = Offsets::kBuySell;
    static constexpr size_t kShares = Offsets::kShares;
    static constexpr size_t kStock = Offsets::kStock;
    static constexpr size_t kPrice = Offsets::kPrice;

protected:
    bool decode_add_body(const std::vector<uint8_t>& data, char expected_type, size_t min_size);

    uint64_t order_reference_number_{};
    char side_{};
    uint32_t shares_{};
    std::array<char, 8> stock_{};
    uint32_t price_{};
};

}  // namespace itch
