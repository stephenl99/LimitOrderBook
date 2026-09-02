//
// Created by Stephen Linder on 8/22/26.
//

#include "SecurityBook.h"

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

SecurityBook::SecurityBook(uint16_t stock_locate)
    : stock_locate_(stock_locate)
{
}

void SecurityBook::insert(std::unique_ptr<Order>&& order)
{
    const uint32_t price = order->price();
    const uint64_t ref = order->order_reference_number();
    const Side side = order->side();

    if (side == Side::ASK) {
        auto& level = ask_levels[price];
        const auto it = level.orders.insert(level.orders.end(), std::move(order));
        order_mapping.emplace(ref,
                              OrderIterator(level, it, price, side));
    } else {
        auto& level = bid_levels[price];
        const auto it = level.orders.insert(level.orders.end(), std::move(order));
        order_mapping.emplace(ref, OrderIterator(level, it, price, side));
    }
}

void SecurityBook::remove_order(uint64_t order_reference_number)
{
    auto map_it = order_mapping.find(order_reference_number);
    if (map_it == order_mapping.end()) {
        return;
    }

    OrderIterator& handle = map_it->second;
    handle.level.orders.erase(handle.it);
    erase_empty_level(bid_levels, ask_levels, handle);
    order_mapping.erase(map_it);
}

void SecurityBook::delete_order(uint64_t order_reference_number)
{
    if (!order_mapping.contains(order_reference_number)) {
        std::cout << "Attempted to delete order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }
    remove_order(order_reference_number);
}

void SecurityBook::reduce_order_quantity(uint64_t order_reference_number,
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
    const uint32_t quantity_remaining = handle.it->get()->quantity();

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

    handle.it->get()->set_quantity(new_quantity);
}

void SecurityBook::execute_order(uint64_t order_reference_number, uint32_t executed_shares)
{
    reduce_order_quantity(order_reference_number, executed_shares, "execute");
}

void SecurityBook::cancel_order(uint64_t order_reference_number, uint32_t cancelled_shares)
{
    reduce_order_quantity(order_reference_number, cancelled_shares, "cancel");
}

void SecurityBook::replace_order(uint64_t old_order_reference_number,
                         uint64_t new_order_reference_number,
                         uint32_t new_price,
                         uint32_t new_shares)
{
    auto map_it = order_mapping.find(old_order_reference_number);
    if (map_it == order_mapping.end()) {
        std::cout << "Attempted to replace order " << old_order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }

    std::unique_ptr<Order>::pointer old_order = map_it->second.it->get();
    const uint64_t timestamp_ns = old_order->timestamp_ns();
    const char side = (old_order->side() == Side::BID) ? 'B' : 'S';

    remove_order(old_order_reference_number);

    insert(std::make_unique<Order>(timestamp_ns,
                      old_order->stock_locate(),
                      new_order_reference_number,
                      side,
                      new_price,
                      new_shares));
}
