//
// Created by Stephen Linder on 8/31/26.
//

#include "Book.h"

SecurityBook& Book::book_for(uint16_t stock_locate)
{
    auto& slot = security_books[stock_locate];
    if (!slot) {
        slot = std::make_unique<SecurityBook>(stock_locate);
    }
    return *slot;
}

std::optional<SecurityBook *> Book::find(uint16_t stock_locate) const
{
    const auto it = security_books.find(stock_locate);
    if (it == security_books.end() || !it->second) {
        return {};
    }
    return it->second.get();
}

std::vector<uint16_t> Book::security_locates() const
{
    std::vector<uint16_t> locates;
    locates.reserve(security_books.size());
    for (const auto& entry : security_books) {
        if (entry.second) {
            locates.push_back(entry.first);
        }
    }
    return locates;
}
