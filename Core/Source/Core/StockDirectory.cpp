//
// Created by Stephen Linder on 8/31/26.
//

#include "StockDirectory.h"

#include <string>

namespace {

std::string trim_stock(const char* raw8)
{
    std::string symbol(raw8, 8);
    while (!symbol.empty() && symbol.back() == ' ') {
        symbol.pop_back();
    }
    return symbol;
}

}  // namespace

void StockDirectory::add(uint16_t locate, const char* stock_raw8)
{
    directory_[locate] = trim_stock(stock_raw8);
}

std::optional<std::string> StockDirectory::lookup(uint16_t locate) const
{
    const auto it = directory_.find(locate);
    if (it == directory_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string StockDirectory::translate(uint16_t locate) const
{
    const auto symbol = lookup(locate);
    if (!symbol.has_value()) {
        return {};
    }
    return *symbol;
}

std::vector<uint16_t> StockDirectory::locates() const
{
    std::vector<uint16_t> locates;
    locates.reserve(directory_.size());
    for (const auto& entry : directory_) {
        locates.push_back(entry.first);
    }
    return locates;
}
