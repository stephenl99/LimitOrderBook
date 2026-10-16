#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

#include "Book/InnerBook/LevelSummary.h"
#include "Book/InnerBook/LevelSearch.h"

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
    std::vector<Entry> entries_;
};

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

template <typename Better>
class PriceLadder {
    struct Slot {
        uint32_t quantity = 0;
        uint32_t orders = 0;
    };

public:
    void set_tick(Price tick) { tick_ = tick; }

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

    std::optional<Price> best() const
    {
        if (best_ < 0) {
            return std::nullopt;
        }
        return price_at(best_);
    }

    void clear()
    {
        slots_.clear();
        base_ = 0;
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
    // these things can be static since each template initializes them separately

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
        const auto index = static_cast<std::ptrdiff_t>((price - base_) / tick_);
        return in_range(index) ? index : -1;
    }

    std::ptrdiff_t slot_for(Price price)
    {
        if (slots_.empty()) {
            if (tick_ <= 0) {
                throw std::logic_error("PriceLadder needs set_tick before the first add");
            }
            base_ = price;
            slots_.resize(1);
            return 0;
        }
        auto index = static_cast<std::ptrdiff_t>((price - base_) / tick_);
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

    std::vector<Slot> slots_;
    Price base_ = 0;
    Price tick_ = 0;
    std::size_t levels_ = 0;
    std::ptrdiff_t best_ = -1;
};
