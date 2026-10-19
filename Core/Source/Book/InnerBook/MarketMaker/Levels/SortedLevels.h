#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "Book/InnerBook/LevelSearch.h"
#include "Book/InnerBook/LevelSummary.h"

template <typename Better, std::size_t LinearProbes = 8>
class SortedLevels {
    struct Entry {
        Price price;
        uint32_t quantity;
        uint32_t orders;
    };
    using Entries = std::vector<Entry>;

public:
    void add(Price price, uint32_t quantity)
    {
        const auto [index, found] = locate_level<Better, LinearProbes, Entries>(entries_, price);
        if (!found) {
            entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(index), Entry{price, quantity, 1});
            return;
        }
        Entry& entry = entries_[index];
        entry.quantity += quantity;
        ++entry.orders;
    }

    void remove(Price price, uint32_t quantity)
    {
        const auto [index, found] = locate_level<Better, LinearProbes, Entries>(entries_, price);
        if (!found) {
            return;
        }
        Entry& entry = entries_[index];
        entry.quantity -= quantity;
        if (--entry.orders == 0) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    void adjust(Price price, int64_t quantity_change)
    {
        const auto [index, found] = locate_level<Better, LinearProbes, Entries>(entries_, price);
        if (found) {
            Entry& entry = entries_[index];
            entry.quantity = static_cast<uint32_t>(static_cast<int64_t>(entry.quantity) + quantity_change);
        }
    }

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

    std::optional<Price> best() const
    {
        if (entries_.empty()) {
            return std::nullopt;
        }
        return entries_.back().price;
    }

    std::vector<LevelSummary> depth(std::size_t levels) const
    {
        std::vector<LevelSummary> result;
        for (auto entry = entries_.rbegin(); entry != entries_.rend() && result.size() < levels; ++entry) {
            result.push_back(LevelSummary{entry->price, entry->quantity});
        }
        return result;
    }

private:
    Entries entries_;
};
