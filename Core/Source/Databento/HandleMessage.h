#pragma once

#include <memory>

#include "Core/Book.h"
#include "Itch/Message.h"

namespace databento {

void handle_message(const std::unique_ptr<itch::Message>& message, Book* book);

}  // namespace databento
