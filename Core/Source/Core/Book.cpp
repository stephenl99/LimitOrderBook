//
// Created by Stephen Linder on 8/31/26.
//

#include "Book.h"

SecurityBook& Book::book_for(uint16_t stock_locate)
{
    auto& slot = books_[stock_locate];
    if (!slot) {
        slot = std::make_unique<SecurityBook>(stock_locate);
    }
    return *slot;
}

std::optional<SecurityBook *> Book::find(uint16_t stock_locate) const
{
    const auto it = books_.find(stock_locate);
    if (it == books_.end() || !it->second) {
        return {};
    }
    return it->second.get();
}
