//
// Created by Stephen Linder on 8/22/26.
//

#include "Book.h"

#include <iostream>

namespace {

void erase_empty_level(std::map<uint32_t, Level, std::greater<>>& bid_levels,
                       std::map<uint32_t, Level, std::less<>>& ask_levels,
                       const OrderIterator& handle)
{
    if (!handle.level.orders.empty()) {
        return;
    }
    if (handle.side == Side::ASK) {
        ask_levels.erase(handle.price);
    } else {
        bid_levels.erase(handle.price);
    }
}

}  // namespace

void Book::insert(Order& order)
{
    const uint32_t price = order.price();
    const uint64_t ref = order.order_reference_number();
    const Side side = order.side();

    if (side == Side::ASK) {
        auto& level = ask_levels[price];
        auto it = level.orders.insert(level.orders.end(), std::move(order));
        order_mapping.emplace(ref,
                              std::make_unique<OrderIterator>(OrderIterator{.level = level, .it = it, .price = price, .side = side}));
    } else {
        auto& level = bid_levels[price];
        auto it = level.orders.insert(level.orders.end(), std::move(order));
        order_mapping.emplace(ref,
                              std::make_unique<OrderIterator>(OrderIterator{.level = level, .it = it, .price = price, .side = side}));
    }
}

void Book::remove_order(uint64_t order_reference_number)
{
    auto map_it = order_mapping.find(order_reference_number);
    if (map_it == order_mapping.end()) {
        return;
    }

    OrderIterator& handle = *map_it->second;
    handle.level.orders.erase(handle.it);
    erase_empty_level(bid_levels, ask_levels, handle);
    order_mapping.erase(map_it);
}

void Book::delete_order(uint64_t order_reference_number)
{
    if (!order_mapping.contains(order_reference_number)) {
        std::cout << "Attempted to delete order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }
    remove_order(order_reference_number);
}

void Book::execute_order(uint64_t order_reference_number, uint32_t executed_shares)
{
    auto map_it = order_mapping.find(order_reference_number);
    if (map_it == order_mapping.end()) {
        std::cout << "Attempted to execute order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }

    auto& handle = *map_it->second;
    const uint32_t quantity_remaining = handle.it->quantity();

    if (executed_shares > quantity_remaining) {
        std::cout << "Attempted to execute " << executed_shares << " shares of order "
                  << order_reference_number << ", but only " << quantity_remaining
                  << " remained" << std::endl;
        remove_order(order_reference_number);
        return;
    }

    const uint32_t new_quantity = quantity_remaining - executed_shares;
    if (new_quantity == 0) {
        remove_order(order_reference_number);
        return;
    }

    handle.it->set_quantity(new_quantity);
}
