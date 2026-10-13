#pragma once

#include <memory>
#include <span>

#include "Message/Itch/Message.h"

namespace itch {

std::unique_ptr<Message> decode_message(std::span<const uint8_t> data);

}  // namespace itch
