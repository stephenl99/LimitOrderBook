#pragma once

#include <memory>
#include <vector>

#include "Message/Databento/MboMessage.h"

namespace mbo {

std::unique_ptr<MboMessage> decode_message(const std::vector<uint8_t>& data);

}  // namespace mbo
