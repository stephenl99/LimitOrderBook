#pragma once

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

#include "Level.h"
#include "LevelSearch.h"

template <typename Better, std::size_t LinearProbes = 8>
class SortedLevels {
    struct Entry {
        Price price;
        uint32_t quantity;
        uint32_t orders;
    };

public:
    void add(Price price, uint32_t quantity)
    {
        const auto [index, found] = locate_level<Better, LinearProbes>(entries_, price);
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
        const auto [index, found] = locate_level<Better, LinearProbes>(entries_, price);
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
        const auto [index, found] = locate_level<Better, LinearProbes>(entries_, price);
        if (found) {
            Entry& entry = entries_[index];
            entry.quantity = static_cast<uint32_t>(static_cast<int64_t>(entry.quantity) + quantity_change);
        }
    }

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

    std::vector<LevelSummary> depth(std::size_t levels) const
    {
        std::vector<LevelSummary> result;
        for (auto entry = entries_.rbegin(); entry != entries_.rend() && result.size() < levels; ++entry) {
            result.push_back(LevelSummary{entry->price, entry->quantity});
        }
        return result;
    }

private:
    std::vector<Entry> entries_;
};

template <typename Better>
class PriceLadder {
    struct Slot {
        uint32_t quantity = 0;
        uint32_t orders = 0;
    };

public:
    void add(Price price, uint32_t quantity)
    {
        const std::ptrdiff_t index = slot_for(price);
        Slot& slot = slots_[static_cast<std::size_t>(index)];
        slot.quantity += quantity;
        if (slot.orders++ == 0) {
            ++levels_;
            if (best_ < 0 || better(index, best_)) {
                best_ = index;
            }
        }
    }

    void remove(Price price, uint32_t quantity)
    {
        const std::ptrdiff_t index = existing_slot(price);
        if (index < 0) {
            return;
        }
        Slot& slot = slots_[static_cast<std::size_t>(index)];
        slot.quantity -= quantity;
        if (--slot.orders == 0) {
            --levels_;
            if (index == best_) {
                best_ = next_best(index);
            }
        }
    }

    void adjust(Price price, int64_t quantity_change)
    {
        const std::ptrdiff_t index = existing_slot(price);
        if (index >= 0) {
            Slot& slot = slots_[static_cast<std::size_t>(index)];
            slot.quantity = static_cast<uint32_t>(static_cast<int64_t>(slot.quantity) + quantity_change);
        }
    }

    std::size_t size() const { return levels_; }
    bool empty() const { return levels_ == 0; }

    void clear()
    {
        slots_.clear();
        base_ = 0;
        tick_ = 0;
        levels_ = 0;
        best_ = -1;
    }

    std::vector<LevelSummary> depth(std::size_t levels) const
    {
        std::vector<LevelSummary> result;
        for (std::ptrdiff_t index = best_; in_range(index) && result.size() < levels; index += kWorseStep) {
            const Slot& slot = slots_[static_cast<std::size_t>(index)];
            if (slot.orders != 0) {
                result.push_back(LevelSummary{price_at(index), slot.quantity});
            }
        }
        return result;
    }

private:
    static constexpr bool kHigherIsBetter = Better{}(Price{1}, Price{0});
    static constexpr std::ptrdiff_t kWorseStep = kHigherIsBetter ? -1 : 1;

    static bool better(std::ptrdiff_t a, std::ptrdiff_t b) { return kHigherIsBetter ? a > b : a < b; }

    bool in_range(std::ptrdiff_t index) const
    {
        return index >= 0 && index < static_cast<std::ptrdiff_t>(slots_.size());
    }

    Price price_at(std::ptrdiff_t index) const { return base_ + static_cast<Price>(index) * tick_; }

    std::ptrdiff_t next_best(std::ptrdiff_t from) const
    {
        if (levels_ == 0) {
            return -1;
        }
        for (std::ptrdiff_t index = from + kWorseStep; in_range(index); index += kWorseStep) {
            if (slots_[static_cast<std::size_t>(index)].orders != 0) {
                return index;
            }
        }
        return -1;
    }

    std::ptrdiff_t existing_slot(Price price) const
    {
        if (slots_.empty()) {
            return -1;
        }
        const Price offset = price - base_;
        if (tick_ == 0) {
            return offset == 0 ? 0 : -1;
        }
        if (offset % tick_ != 0) {
            return -1;
        }
        const auto index = static_cast<std::ptrdiff_t>(offset / tick_);
        return in_range(index) ? index : -1;
    }

    std::ptrdiff_t slot_for(Price price)
    {
        if (slots_.empty()) {
            base_ = price;
            slots_.resize(1);
            return 0;
        }
        const Price offset = price - base_;
        if (offset != 0) {
            const Price distance = offset < 0 ? -offset : offset;
            if (tick_ == 0) {
                tick_ = distance;
            } else if (distance % tick_ != 0) {
                refine_tick(std::gcd(tick_, distance));
            }
        }
        auto index = static_cast<std::ptrdiff_t>(tick_ == 0 ? 0 : (price - base_) / tick_);
        if (index < 0) {
            const auto grow = static_cast<std::size_t>(-index) + slack();
            slots_.insert(slots_.begin(), grow, Slot{});
            base_ -= static_cast<Price>(grow) * tick_;
            if (best_ >= 0) {
                best_ += static_cast<std::ptrdiff_t>(grow);
            }
            index += static_cast<std::ptrdiff_t>(grow);
        } else if (index >= static_cast<std::ptrdiff_t>(slots_.size())) {
            slots_.resize(static_cast<std::size_t>(index) + 1 + slack());
        }
        return index;
    }

    std::size_t slack() const { return slots_.size() / 2 + 16; }

    void refine_tick(Price finer_tick)
    {
        const auto ratio = static_cast<std::size_t>(tick_ / finer_tick);
        std::vector<Slot> refined((slots_.size() - 1) * ratio + 1);
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            refined[index * ratio] = slots_[index];
        }
        slots_ = std::move(refined);
        if (best_ >= 0) {
            best_ *= static_cast<std::ptrdiff_t>(ratio);
        }
        tick_ = finer_tick;
    }

    std::vector<Slot> slots_;
    Price base_ = 0;
    Price tick_ = 0;
    std::size_t levels_ = 0;
    std::ptrdiff_t best_ = -1;
};
