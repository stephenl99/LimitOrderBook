//
// Created by Stephen Linder on 8/22/26.
//
#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>

#include "Level.h"
#include "Order.h"

struct OrderIterator {
    Level& level;
    std::list<Order>::iterator it;
    uint32_t price;
    Side side;
};

class Book {
public:
    std::map<uint32_t, Level, std::greater<>> bid_levels;
    std::map<uint32_t, Level, std::less<>> ask_levels;
    std::unordered_map<uint64_t, std::unique_ptr<OrderIterator>> order_mapping;
    void insert(Order& order);

    void delete_order(uint64_t order_reference_number);

    void execute_order(uint64_t order_reference_number, uint32_t executed_shares);

private:
    void remove_order(uint64_t order_reference_number);
};
