//
// Created by Stephen Linder on 8/22/26.
//
#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <vector>

#include "FlatLevelMap.h"
#include "Level.h"
#include "Order.h"

struct OrderIterator {
    Level* level;
    OrderList::iterator it;
    Price price;
    Side side;
    OrderIterator(Level& level, const OrderList::iterator it,
        const Price price, const Side side) : level(&level), it(it), price(price), side(side) {}
};

struct TreeLevels {
    template <typename Better>
    using Side = std::map<Price, Level, Better>;
};

template <std::size_t LinearProbes>
struct BasicFlatLevels {
    template <typename Better>
    using Side = FlatLevelMap<Better, LinearProbes>;
};

using FlatLevels = BasicFlatLevels<kLinearOnly>;
using HybridFlatLevels = BasicFlatLevels<8>;

template <typename Levels>
class BasicSecurityBook {
    std::pmr::unsynchronized_pool_resource pool_;

public:
    using BidLevels = typename Levels::template Side<std::greater<>>;
    using AskLevels = typename Levels::template Side<std::less<>>;

    explicit BasicSecurityBook(InstrumentId stock_locate = 0);

    InstrumentId stock_locate() const { return stock_locate_; }

    BidLevels bid_levels;
    AskLevels ask_levels;
    std::pmr::unordered_map<uint64_t, OrderIterator> order_mapping{&pool_};
    void insert(const Order& order);

    void add_order(uint64_t order_reference_number, Side side, Price price, uint32_t quantity)
    {
        insert(Order(0, stock_locate_, order_reference_number, side == Side::BID ? 'B' : 'S', price, quantity));
    }

    void delete_order(uint64_t order_reference_number);

    void execute_order(uint64_t order_reference_number, uint32_t executed_shares);

    void cancel_order(uint64_t order_reference_number, uint32_t cancelled_shares);

    void replace_order(uint64_t old_order_reference_number,
                       uint64_t new_order_reference_number,
                       Price new_price,
                       uint32_t new_shares);

    void modify_order(uint64_t order_reference_number, Price new_price, uint32_t new_shares);

    void clear();

    std::size_t resting_orders() const { return order_mapping.size(); }
    std::size_t bid_level_count() const { return bid_levels.size(); }
    std::size_t ask_level_count() const { return ask_levels.size(); }
    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    std::vector<LevelSummary> bid_depth(std::size_t levels) const;
    std::vector<LevelSummary> ask_depth(std::size_t levels) const;

private:
    Level& level_for(Side side, Price price);
    void erase_level_if_empty(const Level& level, Side side, Price price);
    void remove_order(uint64_t order_reference_number);
    void reduce_order_quantity(uint64_t order_reference_number,
                               uint32_t shares,
                               const char* action);

    InstrumentId stock_locate_;
};

extern template class BasicSecurityBook<TreeLevels>;
extern template class BasicSecurityBook<FlatLevels>;
extern template class BasicSecurityBook<HybridFlatLevels>;

using SecurityBook = BasicSecurityBook<TreeLevels>;
using FlatSecurityBook = BasicSecurityBook<FlatLevels>;
using HybridFlatSecurityBook = BasicSecurityBook<HybridFlatLevels>;

template <typename T>
concept LevelMap = std::ranges::range<T> && requires(const T& levels, std::ranges::range_value_t<T> entry) {
    { levels.empty() } -> std::convertible_to<bool>;
    { entry.first } -> std::convertible_to<Price>;
    { entry.second.orders } -> std::ranges::range;
};

template <LevelMap Levels>
std::vector<LevelSummary> depth_snapshot(const Levels& levels, std::size_t depth)
{
    auto summarize = [](const auto& entry) {
        const auto& [price, level] = entry;
        auto shares = level.orders
            | std::views::transform([](const auto& order) { return order.quantity(); });
        const uint32_t total_shares = std::accumulate(shares.begin(), shares.end(), 0u);
        return LevelSummary{price, total_shares};
    };

    auto view = levels
        | std::views::transform(summarize)
        | std::views::take(depth);

    std::vector<LevelSummary> result;
    for (const auto& summary : view) {
        result.push_back(summary);
    }
    return result;
}

struct SumCountPair {
    double sum;
    int count;
};
template <LevelMap Levels>
double vwap(const Levels& levels, std::size_t depth)
{
    const std::vector<LevelSummary> summary = depth_snapshot(levels, depth);
    if (summary.empty()) {
        return 0.0;
    }
    auto weighted_prices = summary
        | std::views::transform([&](const LevelSummary& level) {
              double total_mass_at_price = static_cast<double>(level.price) * level.total_shares;
              return SumCountPair(total_mass_at_price, level.total_shares);
          });
    const auto weighted_sum = std::accumulate(weighted_prices.begin(), weighted_prices.end(), 0.0, [](double acc, const SumCountPair& pair) {
        return acc + pair.sum;
    });
    const auto total = std::accumulate(weighted_prices.begin(), weighted_prices.end(), 0.0, [](double acc, const SumCountPair& pair) {
        return acc + pair.count;
    });

    return weighted_sum / total;
}

template <LevelMap Levels>
std::optional<Price> best_price(const Levels& levels)
{
    if (levels.empty()) {
        return std::nullopt;
    }
    return levels.begin()->first;
}

template <LevelMap Levels>
std::vector<LevelSummary> levels_within_cents(const Levels& levels, Price cents)
{
    const auto best = best_price(levels);
    if (!best.has_value()) {
        return {};
    }

    auto near_best = levels
        | std::views::take_while([&](const auto& entry) {
              return std::abs(entry.first - *best) <= cents;
          });

    std::vector<LevelSummary> result;
    for (const auto& [price, level] : near_best) {
        auto shares = level.orders
            | std::views::transform([](const auto& order) { return order.quantity(); });
        const uint32_t total_shares = std::accumulate(shares.begin(), shares.end(), 0u);
        result.push_back(LevelSummary{price, total_shares});
    }
    return result;
}
