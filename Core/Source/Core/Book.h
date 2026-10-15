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

#include "AggregateSecurityBook.h"
#include "SecurityBook.h"
#include "StockDirectory.h"

template <typename SecurityBookType, std::size_t InstrumentCacheSlots = 0>
class BasicBook {
    static_assert((InstrumentCacheSlots & (InstrumentCacheSlots - 1)) == 0, "cache slots must be zero or a power of two");

public:
    using SecurityBookT = SecurityBookType;

    StockDirectory directory;

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

    std::optional<SecurityBookType*> find(InstrumentId stock_locate) const;
    std::vector<InstrumentId> security_locates() const;
    void absorb(BasicBook&& other);

private:
    struct CacheSlot {
        InstrumentId locate = 0;
        SecurityBookType* book = nullptr;
    };

    static std::size_t cache_index(InstrumentId stock_locate)
    {
        if constexpr (InstrumentCacheSlots <= 1) {
            return 0;
        } else {
            constexpr unsigned kBits = static_cast<unsigned>(std::countr_zero(InstrumentCacheSlots));
            return static_cast<std::size_t>((uint64_t{stock_locate} * 0x9E3779B97F4A7C15ULL) >> (64 - kBits));
        }
    }

    SecurityBookType& find_or_create(InstrumentId stock_locate);

    std::unordered_map<InstrumentId, std::unique_ptr<SecurityBookType>> security_books;
    std::array<CacheSlot, (InstrumentCacheSlots == 0 ? 1 : InstrumentCacheSlots)> cache_{};
};

extern template class BasicBook<SecurityBook>;
extern template class BasicBook<FlatSecurityBook>;
extern template class BasicBook<HybridFlatSecurityBook>;
extern template class BasicBook<SortedAggregateSecurityBook>;
extern template class BasicBook<SortedAggregateFlatIndexSecurityBook>;
extern template class BasicBook<SortedAggregateFlatIndexSecurityBook, 1>;
extern template class BasicBook<LadderSecurityBook>;
extern template class BasicBook<LadderSecurityBook, 1>;
extern template class BasicBook<LadderSecurityBook, 64>;
extern template class BasicBook<IdentityLadderSecurityBook, 64>;

using Book = BasicBook<SecurityBook>;
using FlatBook = BasicBook<FlatSecurityBook>;
using HybridFlatBook = BasicBook<HybridFlatSecurityBook>;
using AggregateBook = BasicBook<SortedAggregateSecurityBook>;
using AggregateFlatIndexBook = BasicBook<SortedAggregateFlatIndexSecurityBook>;
using CachedAggregateFlatIndexBook = BasicBook<SortedAggregateFlatIndexSecurityBook, 1>;
using LadderBook = BasicBook<LadderSecurityBook>;
using CachedLadderBook = BasicBook<LadderSecurityBook, 1>;
using DirectCachedLadderBook = BasicBook<LadderSecurityBook, 64>;
using IdentityLadderBook = BasicBook<IdentityLadderSecurityBook, 64>;
