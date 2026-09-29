#pragma once

#include "Databento/MboMessage.h"

namespace mbo {

class FillMessage : public MboMessage {
public:
    bool decode(const std::vector<uint8_t>& data) override;
};

}  // namespace mbo
