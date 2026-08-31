#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Itch/Message.h"

namespace itch {

class StockDirectoryMessage : public Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] const char* stock() const { return stock_.data(); }
    [[nodiscard]] char market_category() const { return market_category_; }

    static constexpr size_t kPayloadSize = 39;
    static constexpr size_t kStock = 11;
    static constexpr size_t kMarketCategory = 19;

private:
    std::array<char, 8> stock_{};
    char market_category_{};
};

}  // namespace itch
