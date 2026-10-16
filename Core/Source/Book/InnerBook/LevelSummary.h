#pragma once

#include <cstdint>

#include "Order.h"

struct LevelSummary {
    Price price;
    uint32_t total_shares;
};
