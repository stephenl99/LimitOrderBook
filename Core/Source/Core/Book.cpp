//
// Created by Stephen Linder on 8/22/26.
//

#include "Book.h"

void Book::insert(Order& order)
{
    const uint32_t price = order.price();
    if (order.side() == Side::ASK) {
        ask_levels[price].orders.push_back(std::move(order));
    } else {
        bid_levels[price].orders.push_back(std::move(order));
    }
}
