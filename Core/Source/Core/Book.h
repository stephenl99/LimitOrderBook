//
// Created by Stephen Linder on 8/22/26.
//
#pragma once

#include <cstdint>
#include <map>

#include "Level.h"
#include "Order.h"

class Book {
public:
    std::map<uint32_t, Level, std::greater<>> bid_levels;
    std::map<uint32_t, Level, std::less<>> ask_levels;
    void insert(Order& order);
};
