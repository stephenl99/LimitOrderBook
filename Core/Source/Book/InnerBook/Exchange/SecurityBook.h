//
// Created by Stephen Linder on 8/22/26.
//
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <unordered_map>
#include <vector>

#include "FlatLevelMap.h"
#include "Level.h"
#include "Book/InnerBook/LevelSummary.h"
#include "Book/InnerBook/Order.h"

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
