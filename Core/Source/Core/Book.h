//
// Created by Stephen Linder on 8/22/26.
//
#pragma once

#include <cstdint>
#include <map>

#include "Level.h"
#include "Order.h"

struct OrderIterator {
    Order order;
    Level& level;
    std::list<Order>::iterator it;
};
class Book {
public:
    std::map<uint32_t, Level, std::greater<>> bid_levels;
    std::map<uint32_t, Level, std::less<>> ask_levels;
    std::unordered_map<uint64_t, std::unique_ptr<OrderIterator>> order_mapping;
    void insert(Order& order);

    void delete_order(uint64_t order_reference_number);

    void execute_order(uint64_t order_reference_number, uint32_t executed_shares);
};
