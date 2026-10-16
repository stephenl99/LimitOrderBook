#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Book/InnerBook/Order.h"

struct ShadowOrder {
    Side side;
    Price price;
    std::uint32_t quantity;
    std::size_t live_index;
};

class ShadowBook {
public:
    using Queue = std::deque<std::uint64_t>;

    Price fair = 0;

    void add(std::uint64_t ref, Side side, Price price, std::uint32_t quantity)
    {
        levels(side)[price].push_back(ref);
        orders_.emplace(ref, ShadowOrder{side, price, quantity, live_.size()});
        live_.push_back(ref);
        recent_[recent_count_++ % recent_.size()] = ref;
    }

    void remove(std::uint64_t ref)
    {
        const auto found = orders_.find(ref);
        const ShadowOrder order = found->second;

        auto& side_levels = levels(order.side);
        const auto level = side_levels.find(order.price);
        level->second.erase(std::find(level->second.begin(), level->second.end(), ref));
        if (level->second.empty()) {
            side_levels.erase(level);
        }

        const std::uint64_t last = live_.back();
        live_[order.live_index] = last;
        orders_[last].live_index = order.live_index;
        live_.pop_back();
        orders_.erase(found);
    }

    std::optional<Price> best_price(Side side) const
    {
        const auto& side_levels = levels(side);
        if (side_levels.empty()) {
            return std::nullopt;
        }
        return side == Side::BID ? side_levels.rbegin()->first : side_levels.begin()->first;
    }

    Queue* best_queue(Side side)
    {
        auto& side_levels = levels(side);
        if (side_levels.empty()) {
            return nullptr;
        }
        return side == Side::BID ? &side_levels.rbegin()->second : &side_levels.begin()->second;
    }

    ShadowOrder& order(std::uint64_t ref) { return orders_.at(ref); }
    bool contains(std::uint64_t ref) const { return orders_.contains(ref); }

    std::size_t size() const { return live_.size(); }
    bool empty() const { return live_.empty(); }
    std::uint64_t live_order(std::size_t index) const { return live_[index]; }

    std::size_t recent_window() const { return std::min(recent_count_, recent_.size()); }
    std::uint64_t recent_order(std::size_t index) const { return recent_[index]; }

private:
    using Levels = std::map<Price, Queue>;

    Levels& levels(Side side) { return side == Side::BID ? bids_ : asks_; }
    const Levels& levels(Side side) const { return side == Side::BID ? bids_ : asks_; }

    Levels bids_;
    Levels asks_;
    std::unordered_map<std::uint64_t, ShadowOrder> orders_;
    std::vector<std::uint64_t> live_;
    std::array<std::uint64_t, 64> recent_{};
    std::size_t recent_count_ = 0;
};
