//
// Created by Stephen Linder on 8/31/26.
//
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "SecurityBook.h"
#include "StockDirectory.h"

class Book {
public:
    StockDirectory directory;
    SecurityBook& book_for(InstrumentId stock_locate);
    [[nodiscard]] std::optional<SecurityBook *> find(InstrumentId stock_locate) const;
    [[nodiscard]] std::vector<InstrumentId> security_locates() const;

private:
    std::unordered_map<InstrumentId, std::unique_ptr<SecurityBook>> security_books;
};
