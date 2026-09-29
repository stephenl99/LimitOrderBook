#pragma once

#include "Databento/MboMessage.h"

namespace databento {

class TradeMessage : public MboMessage {
public:
    bool decode(const std::vector<uint8_t>& data) override;
};

}  // namespace databento
