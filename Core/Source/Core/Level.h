#pragma once

#include <list>

#include "Order.h"
class Level {
public:
    std::list<std::unique_ptr<Order>> orders;
};
