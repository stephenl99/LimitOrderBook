#pragma once

#include <memory>

#include "Book/OuterBook/ItchBook.h"
#include "Message/Itch/Message.h"

namespace itch {

void handle_message(const std::unique_ptr<Message>& message, ItchBook* book);

}  // namespace itch
