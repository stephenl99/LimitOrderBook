#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "Book/InnerBook/LevelSummary.h"
#include "Book/InnerBook/MarketMaker/Levels/MapLevels.h"
#include "Book/InnerBook/MarketMaker/Levels/PriceLadder.h"
#include "Book/InnerBook/MarketMaker/Levels/SortedLevels.h"
#include "Book/InnerBook/Order.h"
#include "OrderIndex.h"

template <std::size_t LinearProbes>
struct BasicSortedAggregateLevels {
    template <typename Better>
    using Side = SortedLevels<Better, LinearProbes>;
};

using SortedAggregateLevels = BasicSortedAggregateLevels<8>;
using LinearAggregateLevels = BasicSortedAggregateLevels<kLinearOnly>;

struct MapAggregateLevels {
    template <typename Better>
    using Side = MapLevels<Better>;
};

struct LadderLevels {
    template <typename Better>
    using Side = PriceLadder<Better>;
};

template <typename Levels, typename Orders>
class AggregateSecurityBook {
public:
    using BidLevels = typename Levels::template Side<std::greater<>>;
    using AskLevels = typename Levels::template Side<std::less<>>;

    explicit AggregateSecurityBook(InstrumentId stock_locate = 0) : stock_locate_(stock_locate) {}

    InstrumentId stock_locate() const { return stock_locate_; }

    void set_tick(Price tick)
        requires requires(BidLevels& bids, AskLevels& asks) {
            bids.set_tick(tick);
            asks.set_tick(tick);
        }
    {
        bid_levels_.set_tick(tick);
        ask_levels_.set_tick(tick);
    }

    void add_order(uint64_t order_reference_number, Side side, Price price, uint32_t quantity)
    {
        add_resting(order_reference_number, side, price, quantity);
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

    std::size_t resting_orders() const { return orders_.size(); }
    std::size_t bid_level_count() const { return bid_levels_.size(); }
    std::size_t ask_level_count() const { return ask_levels_.size(); }
    std::optional<Price> best_bid() const { return bid_levels_.best(); }
    std::optional<Price> best_ask() const { return ask_levels_.best(); }
    std::vector<LevelSummary> bid_depth(std::size_t levels) const { return bid_levels_.depth(levels); }
    std::vector<LevelSummary> ask_depth(std::size_t levels) const { return ask_levels_.depth(levels); }

    template <typename Visit>
    void for_each_order(Visit&& visit) const
    {
        orders_.for_each(visit);
    }

private:
    template <typename Action>
    void on_side(Side side, Action&& action)
    {
        if (side == Side::ASK) {
            action(ask_levels_);
        } else {
            action(bid_levels_);
        }
    }

    void add_resting(uint64_t id, Side side, Price price, uint32_t quantity);
    void remove_resting(typename Orders::Handle handle);
    void reduce_order_quantity(uint64_t id, uint32_t shares, const char* action);

    BidLevels bid_levels_;
    AskLevels ask_levels_;
    Orders orders_;
    uint64_t next_sequence_ = 0;
    InstrumentId stock_locate_;
};

extern template class AggregateSecurityBook<SortedAggregateLevels, NodeOrderIndex>;
extern template class AggregateSecurityBook<SortedAggregateLevels, FlatOrderIndex>;
extern template class AggregateSecurityBook<LadderLevels, FlatOrderIndex>;
extern template class AggregateSecurityBook<LadderLevels, IdentityFlatOrderIndex>;
extern template class AggregateSecurityBook<MapAggregateLevels, FlatOrderIndex>;
extern template class AggregateSecurityBook<LinearAggregateLevels, FlatOrderIndex>;

using SortedAggregateSecurityBook = AggregateSecurityBook<SortedAggregateLevels, NodeOrderIndex>;
using SortedAggregateFlatIndexSecurityBook = AggregateSecurityBook<SortedAggregateLevels, FlatOrderIndex>;
using LadderSecurityBook = AggregateSecurityBook<LadderLevels, FlatOrderIndex>;
using IdentityLadderSecurityBook = AggregateSecurityBook<LadderLevels, IdentityFlatOrderIndex>;
using MapAggregateSecurityBook = AggregateSecurityBook<MapAggregateLevels, FlatOrderIndex>;
using LinearAggregateSecurityBook = AggregateSecurityBook<LinearAggregateLevels, FlatOrderIndex>;
