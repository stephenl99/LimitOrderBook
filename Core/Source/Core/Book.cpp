//
// Created by Stephen Linder on 8/31/26.
//

#include "Book.h"

SecurityBook& Book::book_for(InstrumentId stock_locate)
{
    auto& slot = security_books[stock_locate];
    if (!slot) {
        slot = std::make_unique<SecurityBook>(stock_locate);
    }
    return *slot;
}

std::optional<SecurityBook *> Book::find(InstrumentId stock_locate) const
{
    const auto it = security_books.find(stock_locate);
    if (it == security_books.end() || !it->second) {
        return {};
    }
    return it->second.get();
}

std::vector<InstrumentId> Book::security_locates() const
{
    std::vector<InstrumentId> locates;
    locates.reserve(security_books.size());
    for (const auto& entry : security_books) {
        if (entry.second) {
            locates.push_back(entry.first);
        }
    }
    return locates;
}
