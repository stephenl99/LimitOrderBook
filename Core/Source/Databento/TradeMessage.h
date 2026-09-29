#pragma once

#include "Databento/MboMessage.h"

namespace mbo {

class TradeMessage : public MboMessage {
public:
    bool decode(const std::vector<uint8_t>& data) override;
};

}  // namespace mbo
