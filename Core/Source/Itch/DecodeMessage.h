#pragma once

#include <memory>
#include <vector>

#include "Itch/Message.h"

namespace itch {

std::unique_ptr<Message> decode_message(const std::vector<uint8_t>& data);

}  // namespace itch
