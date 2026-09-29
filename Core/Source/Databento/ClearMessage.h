#pragma once

#include "Databento/MboMessage.h"

namespace databento {

class ClearMessage : public MboMessage {
public:
    bool decode(const std::vector<uint8_t>& data) override;
};

}  // namespace databento
