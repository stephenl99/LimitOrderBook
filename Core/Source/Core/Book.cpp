//
// Created by Stephen Linder on 8/22/26.
//

#include "Book.h"

#include <iostream>

void Book::insert(Order& order)
{
    const uint32_t price = order.price();
    if (order.side() == Side::ASK) {
        ask_levels[price].orders.push_back(std::move(order));
        order_mapping.emplace(order.order_reference_number(), make_unique<OrderIterator>(order, ask_levels[price], prev(ask_levels[price].orders.end())));
    } else {
        bid_levels[price].orders.push_back(std::move(order));
        order_mapping.emplace(order.order_reference_number(), make_unique<OrderIterator>(order, bid_levels[price], prev(bid_levels[price].orders.end())));
    }

}
void Book::delete_order(uint64_t order_reference_number) {
    if (!order_mapping.contains(order_reference_number)) {
        std::cout << "Attempted to delete order " << order_reference_number << ", but it was no longer in the book" << std::endl;
        return;
    }
    auto it = order_mapping.at(order_reference_number)->it;
    order_mapping.at(order_reference_number)->level.orders.erase(it);
}

void Book::execute_order(uint64_t order_reference_number, uint32_t executed_shares) {
    if (!order_mapping.contains(order_reference_number)) {
        std::cout << "Attempted to execute order " << order_reference_number << ", but it was no longer in the book" << std::endl;
        return;
    }
    auto it = order_mapping.at(order_reference_number)->it;
    uint32_t quantity_remaining = it->quantity();
    if (quantity_remaining >= executed_shares) {
        it->set_quantity(quantity_remaining - executed_shares);
        return;
    }
    order_mapping.at(order_reference_number)->level.orders.erase(it);
}
