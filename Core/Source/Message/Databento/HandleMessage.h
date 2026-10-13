#pragma once

#include <memory>

#include "Core/Book.h"
#include "Message/Databento/MboMessage.h"

namespace mbo {

void handle_message(const std::unique_ptr<MboMessage>& mbo, Book* book);

}  // namespace mbo
