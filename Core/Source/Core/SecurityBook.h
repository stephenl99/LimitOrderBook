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
    std::list<std::unique_ptr<Order>>::iterator it;
    uint32_t price;
    Side side;
    OrderIterator(Level& level, const std::list<std::unique_ptr<Order>>::iterator it,
        const uint32_t price, const Side side) : level(level), it(it), price(price), side(side) {}
};

class SecurityBook {
public:
    explicit SecurityBook(uint16_t stock_locate = 0);

    [[nodiscard]] uint16_t stock_locate() const { return stock_locate_; }

    std::map<uint32_t, Level, std::greater<>> bid_levels;
    std::map<uint32_t, Level, std::less<>> ask_levels;
    std::unordered_map<uint64_t, OrderIterator> order_mapping;
    void insert(std::unique_ptr<Order> &&order);

    void delete_order(uint64_t order_reference_number);

    void execute_order(uint64_t order_reference_number, uint32_t executed_shares);

    void cancel_order(uint64_t order_reference_number, uint32_t cancelled_shares);

    void replace_order(uint64_t old_order_reference_number,
                       uint64_t new_order_reference_number,
                       uint32_t new_price,
                       uint32_t new_shares);

private:
    void remove_order(uint64_t order_reference_number);
    void reduce_order_quantity(uint64_t order_reference_number,
                               uint32_t shares,
                               const char* action);

    uint16_t stock_locate_;
};
