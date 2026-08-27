#pragma once

#include <cstdint>
#include <vector>

#include "Itch/OrderExecutedMessage.h"

namespace itch {

class OrderExecutedWithPriceMessage : public OrderExecutedMessage {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] char printable() const { return printable_; }
    [[nodiscard]] uint32_t execution_price() const { return execution_price_; }

    static constexpr size_t kPayloadSize = 36;
    static constexpr size_t kPrintable = 32;
    static constexpr size_t kExecutionPrice = 33;

private:
    char printable_{};
    uint32_t execution_price_{};
};

}  // namespace itch
