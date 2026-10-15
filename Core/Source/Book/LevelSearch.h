#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

#include "Order.h"

inline constexpr std::size_t kLinearOnly = std::numeric_limits<std::size_t>::max();

struct LevelPosition {
    std::size_t index;
    bool found;
};

template <typename Better, std::size_t LinearProbes, typename Entries>
LevelPosition locate_level(const Entries& entries, Price price)
{
    std::size_t index = entries.size();
    for (std::size_t probe = 0; probe < LinearProbes && index > 0; ++probe) {
        const Price candidate = entries[index - 1].price;
        if (candidate == price) {
            return {index - 1, true};
        }
        if (Better{}(price, candidate)) {
            return {index, false};
        }
        --index;
    }
    const auto first = entries.begin();
    const auto position = std::partition_point(first, first + static_cast<std::ptrdiff_t>(index),
                                               [price](const auto& entry) { return Better{}(price, entry.price); });
    const auto offset = static_cast<std::size_t>(position - first);
    return {offset, offset < index && entries[offset].price == price};
}
