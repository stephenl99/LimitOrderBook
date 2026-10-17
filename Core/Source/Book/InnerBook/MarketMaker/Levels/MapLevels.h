#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "Book/InnerBook/LevelSummary.h"

template <typename Better>
class MapLevels {
    struct Entry {
        uint32_t quantity = 0;
        uint32_t orders = 0;
    };

public:
    void add(Price price, uint32_t quantity)
    {
        Entry& entry = entries_[price];
        entry.quantity += quantity;
        ++entry.orders;
    }

    void remove(Price price, uint32_t quantity)
    {
        const auto found = entries_.find(price);
        if (found == entries_.end()) {
            return;
        }
        found->second.quantity -= quantity;
        if (--found->second.orders == 0) {
            entries_.erase(found);
        }
    }

    void adjust(Price price, int64_t quantity_change)
    {
        const auto found = entries_.find(price);
        if (found != entries_.end()) {
            Entry& entry = found->second;
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
        return entries_.begin()->first;
    }

    std::vector<LevelSummary> depth(std::size_t levels) const
    {
        std::vector<LevelSummary> result;
        for (auto entry = entries_.begin(); entry != entries_.end() && result.size() < levels; ++entry) {
            result.push_back(LevelSummary{entry->first, entry->second.quantity});
        }
        return result;
    }

private:
    std::map<Price, Entry, Better> entries_;
};
