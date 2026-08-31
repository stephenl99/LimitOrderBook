//
// Created by Stephen Linder on 8/31/26.
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class StockDirectory {
public:
    StockDirectory() = default;

    void add(uint16_t locate, const char* stock_raw8);
    [[nodiscard]] std::optional<std::string> lookup(uint16_t locate) const;
    [[nodiscard]] std::string translate(uint16_t locate) const;
    [[nodiscard]] std::vector<uint16_t> locates() const;

private:
    std::unordered_map<uint16_t, std::string> directory_;
};
