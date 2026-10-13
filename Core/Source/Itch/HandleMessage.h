#pragma once

#include <memory>

#include "Core/Book.h"
#include "Itch/Message.h"

namespace itch {

void handle_message(const std::unique_ptr<Message>& message, Book* book);

}  // namespace itch
