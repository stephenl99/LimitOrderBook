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
    unique_ptr<Book> decode_file(const fs::path &input_path);

    optional<Order> parse_into_order(const std::vector<uint8_t> &data);
};


