//
// Created by Stephen Linder on 8/31/26.
//

#include "Book.h"

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
SecurityBookType& BasicBook<SecurityBookType, InstrumentCacheSlots>::find_or_create(InstrumentId stock_locate)
{
    auto& slot = security_books[stock_locate];
    if (!slot) {
        slot = std::make_unique<SecurityBookType>(stock_locate);
    }
    return *slot;
}

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
std::optional<SecurityBookType*> BasicBook<SecurityBookType, InstrumentCacheSlots>::find(InstrumentId stock_locate) const
{
    const auto it = security_books.find(stock_locate);
    if (it == security_books.end() || !it->second) {
        return {};
    }
    return it->second.get();
}

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
std::vector<InstrumentId> BasicBook<SecurityBookType, InstrumentCacheSlots>::security_locates() const
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

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
void BasicBook<SecurityBookType, InstrumentCacheSlots>::absorb(BasicBook&& other)
{
    for (auto& [locate, security_book] : other.security_books) {
        security_books[locate] = std::move(security_book);
    }
    other.security_books.clear();
    other.cache_ = {};
    cache_ = {};
    directory.absorb(std::move(other.directory));
}

template class BasicBook<SecurityBook>;
template class BasicBook<FlatSecurityBook>;
template class BasicBook<HybridFlatSecurityBook>;
template class BasicBook<SortedAggregateSecurityBook>;
template class BasicBook<SortedAggregateFlatIndexSecurityBook>;
template class BasicBook<SortedAggregateFlatIndexSecurityBook, 1>;
template class BasicBook<LadderSecurityBook>;
template class BasicBook<LadderSecurityBook, 1>;
template class BasicBook<LadderSecurityBook, 64>;
template class BasicBook<IdentityLadderSecurityBook, 64>;
