#pragma once

#include <memory>
#include <vector>

#include "Itch/Message.h"

namespace databento {

std::unique_ptr<itch::Message> decode_message(const std::vector<uint8_t>& data);

}  // namespace databento
