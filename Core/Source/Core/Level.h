#pragma once

#include <cstdint>
#include <list>
#include <memory_resource>

#include "Order.h"

using OrderList = std::pmr::list<Order>;

class Level {
public:
    explicit Level(std::pmr::memory_resource* memory) : orders(memory) {}

    OrderList orders;
};

struct LevelSummary {
    Price price;
    uint32_t total_shares;
};
