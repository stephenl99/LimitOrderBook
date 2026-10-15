//
// Created by Stephen Linder on 8/22/26.
//

#include "SecurityBook.h"

#include <iostream>

namespace {

template <typename Better>
Level& level_at(std::map<Price, Level, Better>& levels, Price price, std::pmr::memory_resource* memory)
{
    return levels.try_emplace(price, memory).first->second;
}

template <typename Better, std::size_t LinearProbes>
Level& level_at(FlatLevelMap<Better, LinearProbes>& levels, Price price, std::pmr::memory_resource* memory)
{
    return levels.level_at(price, memory);
}

}  // namespace

template <typename Levels>
BasicSecurityBook<Levels>::BasicSecurityBook(InstrumentId stock_locate)
    : stock_locate_(stock_locate)
{
}

template <typename Levels>
Level& BasicSecurityBook<Levels>::level_for(Side side, Price price)
{
    if (side == Side::ASK) {
        return level_at(ask_levels, price, &pool_);
    }
    return level_at(bid_levels, price, &pool_);
}

template <typename Levels>
void BasicSecurityBook<Levels>::erase_level_if_empty(const Level& level, Side side, Price price)
{
    if (!level.orders.empty()) {
        return;
    }
    if (side == Side::ASK) {
        ask_levels.erase(price);
    } else {
        bid_levels.erase(price);
    }
}

template <typename Levels>
void BasicSecurityBook<Levels>::insert(const Order& order)
{
    const Price price = order.price();
    const uint64_t ref = order.order_reference_number();
    const Side side = order.side();

    Level& level = level_for(side, price);
    const auto it = level.orders.insert(level.orders.end(), order);
    order_mapping.emplace(ref, OrderIterator(level, it, price, side));
}

template <typename Levels>
void BasicSecurityBook<Levels>::remove_order(uint64_t order_reference_number)
{
    auto map_it = order_mapping.find(order_reference_number);
    if (map_it == order_mapping.end()) {
        return;
    }

    const OrderIterator handle = map_it->second;
    order_mapping.erase(map_it);
    handle.level->orders.erase(handle.it);
    erase_level_if_empty(*handle.level, handle.side, handle.price);
}

template <typename Levels>
void BasicSecurityBook<Levels>::delete_order(uint64_t order_reference_number)
{
    if (!order_mapping.contains(order_reference_number)) {
        std::cout << "Attempted to delete order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }
    remove_order(order_reference_number);
}

template <typename Levels>
void BasicSecurityBook<Levels>::reduce_order_quantity(uint64_t order_reference_number,
                                                      uint32_t shares,
                                                      const char* action)
{
    auto map_it = order_mapping.find(order_reference_number);
    if (map_it == order_mapping.end()) {
        std::cout << "Attempted to " << action << " order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }

    auto& handle = map_it->second;
    const uint32_t quantity_remaining = handle.it->quantity();

    if (shares > quantity_remaining) {
        std::cout << "Attempted to " << action << " " << shares << " shares of order "
                  << order_reference_number << ", but only " << quantity_remaining
                  << " remained" << std::endl;
        remove_order(order_reference_number);
        return;
    }

    const uint32_t new_quantity = quantity_remaining - shares;
    if (new_quantity == 0) {
        remove_order(order_reference_number);
        return;
    }

    handle.it->set_quantity(new_quantity);
}

template <typename Levels>
void BasicSecurityBook<Levels>::execute_order(uint64_t order_reference_number, uint32_t executed_shares)
{
    reduce_order_quantity(order_reference_number, executed_shares, "execute");
}

template <typename Levels>
void BasicSecurityBook<Levels>::cancel_order(uint64_t order_reference_number, uint32_t cancelled_shares)
{
    reduce_order_quantity(order_reference_number, cancelled_shares, "cancel");
}

template <typename Levels>
void BasicSecurityBook<Levels>::replace_order(uint64_t old_order_reference_number,
                                              uint64_t new_order_reference_number,
                                              Price new_price,
                                              uint32_t new_shares)
{
    auto map_it = order_mapping.find(old_order_reference_number);
    if (map_it == order_mapping.end()) {
        std::cout << "Attempted to replace order " << old_order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }

    const Order& old_order = *map_it->second.it;
    const InstrumentId temp_stock_locate = old_order.stock_locate();
    const uint64_t timestamp_ns = old_order.timestamp_ns();
    const char side = (old_order.side() == Side::BID) ? 'B' : 'S';

    remove_order(old_order_reference_number);

    insert(Order(timestamp_ns,
                 temp_stock_locate,
                 new_order_reference_number,
                 side,
                 new_price,
                 new_shares));
}

template <typename Levels>
void BasicSecurityBook<Levels>::modify_order(uint64_t order_reference_number, Price new_price, uint32_t new_shares)
{
    auto map_it = order_mapping.find(order_reference_number);
    if (map_it == order_mapping.end()) {
        std::cout << "Attempted to modify order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }

    OrderIterator& handle = map_it->second;
    Order& order = *handle.it;
    const bool keeps_priority = new_price == order.price() && new_shares <= order.quantity();
    if (keeps_priority && new_shares > 0) {
        order.set_quantity(new_shares);
        return;
    }
    if (new_shares == 0) {
        remove_order(order_reference_number);
        return;
    }

    Level& old_level = *handle.level;
    const Price old_price = handle.price;
    Level& new_level = level_for(handle.side, new_price);
    new_level.orders.splice(new_level.orders.end(), old_level.orders, handle.it);
    order.set_price(new_price);
    order.set_quantity(new_shares);
    handle.level = &new_level;
    handle.price = new_price;
    erase_level_if_empty(old_level, handle.side, old_price);
}

template <typename Levels>
void BasicSecurityBook<Levels>::clear()
{
    order_mapping.clear();
    bid_levels.clear();
    ask_levels.clear();
}

template <typename Levels>
std::optional<Price> BasicSecurityBook<Levels>::best_bid() const
{
    if (bid_levels.empty()) {
        return std::nullopt;
    }
    return (*bid_levels.begin()).first;
}

template <typename Levels>
std::optional<Price> BasicSecurityBook<Levels>::best_ask() const
{
    if (ask_levels.empty()) {
        return std::nullopt;
    }
    return (*ask_levels.begin()).first;
}

template <typename Levels>
std::vector<LevelSummary> BasicSecurityBook<Levels>::bid_depth(std::size_t levels) const
{
    return depth_snapshot(bid_levels, levels);
}

template <typename Levels>
std::vector<LevelSummary> BasicSecurityBook<Levels>::ask_depth(std::size_t levels) const
{
    return depth_snapshot(ask_levels, levels);
}

template class BasicSecurityBook<TreeLevels>;
template class BasicSecurityBook<FlatLevels>;
template class BasicSecurityBook<HybridFlatLevels>;
