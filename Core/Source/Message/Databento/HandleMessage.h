#pragma once

#include "Core/Book.h"
#include "Message/Databento/MboMessage.h"

namespace mbo {

void handle_message(const MboMessage& mbo, Book* book);

}  // namespace mbo
