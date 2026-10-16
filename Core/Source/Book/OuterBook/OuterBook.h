//
// Created by Stephen Linder on 8/31/26.
//
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Book/InnerBook/MarketMaker/AggregateSecurityBook.h"
#include "Book/InnerBook/Exchange/SecurityBook.h"

template <typename SecurityBookType, std::size_t InstrumentCacheSlots = 0>
class OuterBook {
    static_assert(InstrumentCacheSlots == 0 || (InstrumentCacheSlots >= 2 && std::has_single_bit(InstrumentCacheSlots)),
                  "cache slots must be zero or a power of two of at least 2");

public:
    using SecurityBookT = SecurityBookType;

    SecurityBookType& book_for(InstrumentId stock_locate)
    {
        if constexpr (InstrumentCacheSlots == 0) {
            return find_or_create(stock_locate);
        } else {
            CacheSlot& slot = cache_[cache_index(stock_locate)];
            if (slot.book != nullptr && slot.locate == stock_locate) {
                return *slot.book;
            }
            SecurityBookType& book = find_or_create(stock_locate);
            slot = CacheSlot{stock_locate, &book};
            return book;
        }
    }

    void set_tick(InstrumentId stock_locate, Price tick)
    {
        if constexpr (requires(SecurityBookType& book) { book.set_tick(tick); }) {
            find_or_create(stock_locate).set_tick(tick);
        }
    }

    std::optional<SecurityBookType*> find(InstrumentId stock_locate) const;
    std::vector<InstrumentId> security_locates() const;
    void absorb(OuterBook&& other);

private:
    struct CacheSlot {
        InstrumentId locate = 0;
        SecurityBookType* book = nullptr;
    };

    static std::size_t cache_index(InstrumentId stock_locate)
    {
        constexpr unsigned kBits = static_cast<unsigned>(std::countr_zero(InstrumentCacheSlots));
        return static_cast<std::size_t>((uint64_t{stock_locate} * 0x9E3779B97F4A7C15ULL) >> (64 - kBits));
    }

    SecurityBookType& find_or_create(InstrumentId stock_locate);

    std::unordered_map<InstrumentId, std::unique_ptr<SecurityBookType>> security_books;
    std::array<CacheSlot, (InstrumentCacheSlots == 0 ? 1 : InstrumentCacheSlots)> cache_{};
};

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
SecurityBookType& OuterBook<SecurityBookType, InstrumentCacheSlots>::find_or_create(InstrumentId stock_locate)
{
    auto& slot = security_books[stock_locate];
    if (!slot) {
        slot = std::make_unique<SecurityBookType>(stock_locate);
    }
    return *slot;
}

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
std::optional<SecurityBookType*> OuterBook<SecurityBookType, InstrumentCacheSlots>::find(InstrumentId stock_locate) const
{
    const auto it = security_books.find(stock_locate);
    if (it == security_books.end() || !it->second) {
        return {};
    }
    return it->second.get();
}

template <typename SecurityBookType, std::size_t InstrumentCacheSlots>
std::vector<InstrumentId> OuterBook<SecurityBookType, InstrumentCacheSlots>::security_locates() const
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
void OuterBook<SecurityBookType, InstrumentCacheSlots>::absorb(OuterBook&& other)
{
    for (auto& [locate, security_book] : other.security_books) {
        security_books[locate] = std::move(security_book);
    }
    other.security_books.clear();
    other.cache_ = {};
    cache_ = {};
}

extern template class OuterBook<SecurityBook>;
extern template class OuterBook<FlatSecurityBook>;
extern template class OuterBook<HybridFlatSecurityBook>;
extern template class OuterBook<SortedAggregateSecurityBook>;
extern template class OuterBook<SortedAggregateFlatIndexSecurityBook>;
extern template class OuterBook<LadderSecurityBook>;
extern template class OuterBook<LadderSecurityBook, 64>;
extern template class OuterBook<IdentityLadderSecurityBook, 64>;

using Book = OuterBook<SecurityBook>;
using FlatBook = OuterBook<FlatSecurityBook>;
using HybridFlatBook = OuterBook<HybridFlatSecurityBook>;
using AggregateBook = OuterBook<SortedAggregateSecurityBook>;
using AggregateFlatIndexBook = OuterBook<SortedAggregateFlatIndexSecurityBook>;
using LadderBook = OuterBook<LadderSecurityBook>;
using DirectCachedLadderBook = OuterBook<LadderSecurityBook, 64>;
using IdentityLadderBook = OuterBook<IdentityLadderSecurityBook, 64>;
