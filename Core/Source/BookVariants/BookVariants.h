#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "BookVariants/PooledBook.h"
#include "Core/AggregateSecurityBook.h"
#include "Core/Book.h"
#include "Core/SecurityBook.h"

template <typename S>
concept BenchSecurityBook = requires(S book, const S& view, std::uint64_t ref, Side side, Price price, std::uint32_t quantity) {
    book.add_order(ref, side, price, quantity);
    book.execute_order(ref, quantity);
    book.cancel_order(ref, quantity);
    book.delete_order(ref);
    book.replace_order(ref, ref, price, quantity);
    { view.best_bid() } -> std::same_as<std::optional<Price>>;
    { view.best_ask() } -> std::same_as<std::optional<Price>>;
    { view.bid_depth(std::size_t{}) } -> std::same_as<std::vector<LevelSummary>>;
    { view.ask_depth(std::size_t{}) } -> std::same_as<std::vector<LevelSummary>>;
    { view.resting_orders() } -> std::convertible_to<std::size_t>;
};

struct BookVariantInfo {
    std::string_view name;
    std::string_view levels;
    std::string_view index;
    std::string_view orders;
    std::size_t cache_slots;
};

inline constexpr std::size_t kDefaultCacheSlots = 64;

template <typename F>
void for_each_book_variant(F&& visit)
{
    constexpr std::size_t kCache = kDefaultCacheSlots;
    visit(std::type_identity<BasicBook<SecurityBook, kCache>>{}, BookVariantInfo{"tree", "map", "node", "list", kCache});
    visit(std::type_identity<BasicBook<FlatSecurityBook, kCache>>{}, BookVariantInfo{"flat", "vector-linear", "node", "list", kCache});
    visit(std::type_identity<BasicBook<HybridFlatSecurityBook, kCache>>{}, BookVariantInfo{"hybrid", "vector-hybrid", "node", "list", kCache});
    visit(std::type_identity<BasicBook<SortedAggregateSecurityBook, kCache>>{}, BookVariantInfo{"agg", "vector-hybrid", "node", "aggregate", kCache});
    visit(std::type_identity<BasicBook<SortedAggregateFlatIndexSecurityBook, kCache>>{}, BookVariantInfo{"agg-flat", "vector-hybrid", "flat", "aggregate", kCache});
    visit(std::type_identity<BasicBook<LadderSecurityBook, kCache>>{}, BookVariantInfo{"ladder", "ladder", "flat", "aggregate", kCache});
    visit(std::type_identity<BasicBook<IdentityLadderSecurityBook, kCache>>{}, BookVariantInfo{"ladder-identity", "ladder", "flat-identity", "aggregate", kCache});
    visit(std::type_identity<BasicBook<PooledHybridSecurityBook, kCache>>{}, BookVariantInfo{"pooled-hybrid", "vector-hybrid", "flat", "pooled-fifo", kCache});
    visit(std::type_identity<BasicBook<PooledLadderSecurityBook, kCache>>{}, BookVariantInfo{"pooled-ladder", "ladder", "flat", "pooled-fifo", kCache});
    visit(std::type_identity<BasicBook<SecurityBook, 0>>{}, BookVariantInfo{"tree-nocache", "map", "node", "list", 0});
    visit(std::type_identity<BasicBook<LadderSecurityBook, 0>>{}, BookVariantInfo{"ladder-nocache", "ladder", "flat", "aggregate", 0});
    visit(std::type_identity<BasicBook<LadderSecurityBook, 1>>{}, BookVariantInfo{"ladder-cache1", "ladder", "flat", "aggregate", 1});
}

inline std::vector<std::string> book_variant_names()
{
    std::vector<std::string> names;
    for_each_book_variant([&](auto, const BookVariantInfo& info) { names.emplace_back(info.name); });
    return names;
}
