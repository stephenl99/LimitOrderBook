//
// Created by Stephen Linder on 8/25/26.
//

#pragma once

#include <filesystem>
#include <vector>

#include "Book.h"
#include "Order.h"

namespace fs = std::filesystem;

class Decoder {
    public:
    Book *decodeFile(const fs::path &inputPath);

    Order* parseIntoOrder(const std::vector<uint8_t>& data);
};


