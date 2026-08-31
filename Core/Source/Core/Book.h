//
// Created by Stephen Linder on 8/31/26.
//
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include "SecurityBook.h"
#include "StockDirectory.h"

class Book {
public:
    StockDirectory directory;
    SecurityBook& book_for(uint16_t stock_locate);
    [[nodiscard]] std::optional<SecurityBook *> find(uint16_t stock_locate) const;

private:
    std::unordered_map<uint16_t, std::unique_ptr<SecurityBook>> books_;
};
