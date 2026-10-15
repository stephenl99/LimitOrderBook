#pragma once

#include <memory>

#include "Book/Book.h"
#include "Message/Itch/Message.h"

namespace itch {

void handle_message(const std::unique_ptr<Message>& message, Book* book);

}  // namespace itch
