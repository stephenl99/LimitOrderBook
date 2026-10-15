#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

#include "Core/Level.h"
#include "Core/LevelSearch.h"
#include "Core/Order.h"

namespace pooled {

inline constexpr std::uint32_t kNoSlot = std::numeric_limits<std::uint32_t>::max();

class SlotIndex {
public:
    SlotIndex() { rehash(kInitialCapacity); }

    void insert(std::uint64_t ref, std::uint32_t slot)
    {
        if ((size_ + 1) * 2 > entries_.size()) {
            rehash(entries_.size() * 2);
        }
        std::size_t i = home(ref);
        while (entries_[i].ref != kEmpty) {
            if (entries_[i].ref == ref) {
                entries_[i].slot = slot;
                return;
            }
            i = (i + 1) & mask_;
        }
        entries_[i] = {ref, slot};
        ++size_;
    }

    std::uint32_t find(std::uint64_t ref) const
    {
        std::size_t i = home(ref);
        while (entries_[i].ref != kEmpty) {
            if (entries_[i].ref == ref) {
                return entries_[i].slot;
            }
            i = (i + 1) & mask_;
        }
        return kNoSlot;
    }

    void erase(std::uint64_t ref)
    {
        std::size_t hole = home(ref);
        while (entries_[hole].ref != ref) {
            if (entries_[hole].ref == kEmpty) {
                return;
            }
            hole = (hole + 1) & mask_;
        }
        std::size_t next = (hole + 1) & mask_;
        while (entries_[next].ref != kEmpty) {
            const std::size_t wanted = home(entries_[next].ref);
            if (((next - wanted) & mask_) >= ((next - hole) & mask_)) {
                entries_[hole] = entries_[next];
                hole = next;
            }
            next = (next + 1) & mask_;
        }
        entries_[hole].ref = kEmpty;
        --size_;
    }

    std::size_t size() const { return size_; }

private:
    struct Entry {
        std::uint64_t ref;
        std::uint32_t slot;
    };

    static constexpr std::uint64_t kEmpty = std::numeric_limits<std::uint64_t>::max();
    static constexpr std::size_t kInitialCapacity = 1024;

    std::size_t home(std::uint64_t ref) const
    {
        return static_cast<std::size_t>((ref * 0x9E3779B97F4A7C15ull) >> shift_);
    }

    void rehash(std::size_t capacity)
    {
        std::vector<Entry> old = std::move(entries_);
        entries_.assign(capacity, Entry{kEmpty, kNoSlot});
        mask_ = capacity - 1;
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(capacity));
        size_ = 0;
        for (const Entry& entry : old) {
            if (entry.ref != kEmpty) {
                insert(entry.ref, entry.slot);
            }
        }
    }

    std::vector<Entry> entries_;
    std::size_t mask_ = 0;
    unsigned shift_ = 64;
    std::size_t size_ = 0;
};

template <typename Better>
class SortedSlots {
    struct Entry {
        Price price;
        std::uint32_t level;
    };

public:
    template <std::invocable Make>
    std::uint32_t find_or_create(Price price, Make&& make)
    {
        const auto [index, found] = locate_level<Better, 8>(entries_, price);
        if (found) {
            return entries_[index].level;
        }
        const std::uint32_t level = make();
        entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(index), Entry{price, level});
        return level;
    }

    void erase(Price price)
    {
        const auto [index, found] = locate_level<Better, 8>(entries_, price);
        if (found) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    std::uint32_t best() const { return entries_.empty() ? kNoSlot : entries_.back().level; }

    template <std::predicate<std::uint32_t> Visit>
    void for_each(Visit&& visit) const
    {
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (!visit(it->level)) {
                return;
            }
        }
    }

private:
    std::vector<Entry> entries_;
};

template <typename Better>
class LadderSlots {
public:
    template <std::invocable Make>
    std::uint32_t find_or_create(Price price, Make&& make)
    {
        const std::ptrdiff_t index = slot_for(price);
        std::uint32_t& slot = slots_[static_cast<std::size_t>(index)];
        if (slot == kNoSlot) {
            slot = make();
            ++live_;
            if (best_ < 0 || better(index, best_)) {
                best_ = index;
            }
        }
        return slot;
    }

    void erase(Price price)
    {
        const auto index = static_cast<std::ptrdiff_t>(tick_ == 0 ? 0 : (price - base_) / tick_);
        slots_[static_cast<std::size_t>(index)] = kNoSlot;
        if (--live_ == 0) {
            best_ = -1;
        } else if (index == best_) {
            while (slots_[static_cast<std::size_t>(best_)] == kNoSlot) {
                best_ += kWorseStep;
            }
        }
    }

    std::uint32_t best() const { return best_ < 0 ? kNoSlot : slots_[static_cast<std::size_t>(best_)]; }

    template <std::predicate<std::uint32_t> Visit>
    void for_each(Visit&& visit) const
    {
        std::size_t seen = 0;
        for (std::ptrdiff_t index = best_; seen < live_; index += kWorseStep) {
            const std::uint32_t slot = slots_[static_cast<std::size_t>(index)];
            if (slot != kNoSlot) {
                ++seen;
                if (!visit(slot)) {
                    return;
                }
            }
        }
    }

private:
    static constexpr bool kHigherIsBetter = Better{}(Price{1}, Price{0});
    static constexpr std::ptrdiff_t kWorseStep = kHigherIsBetter ? -1 : 1;

    static bool better(std::ptrdiff_t a, std::ptrdiff_t b) { return kHigherIsBetter ? a > b : a < b; }

    std::ptrdiff_t slot_for(Price price)
    {
        if (slots_.empty()) {
            base_ = price;
            slots_.assign(1, kNoSlot);
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
            slots_.insert(slots_.begin(), grow, kNoSlot);
            base_ -= static_cast<Price>(grow) * tick_;
            if (best_ >= 0) {
                best_ += static_cast<std::ptrdiff_t>(grow);
            }
            index += static_cast<std::ptrdiff_t>(grow);
        } else if (index >= static_cast<std::ptrdiff_t>(slots_.size())) {
            slots_.resize(static_cast<std::size_t>(index) + 1 + slack(), kNoSlot);
        }
        return index;
    }

    std::size_t slack() const { return slots_.size() / 2 + 16; }

    void refine_tick(Price finer_tick)
    {
        const auto ratio = static_cast<std::size_t>(tick_ / finer_tick);
        std::vector<std::uint32_t> refined((slots_.size() - 1) * ratio + 1, kNoSlot);
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            refined[index * ratio] = slots_[index];
        }
        slots_ = std::move(refined);
        if (best_ >= 0) {
            best_ *= static_cast<std::ptrdiff_t>(ratio);
        }
        tick_ = finer_tick;
    }

    std::vector<std::uint32_t> slots_;
    Price base_ = 0;
    Price tick_ = 0;
    std::size_t live_ = 0;
    std::ptrdiff_t best_ = -1;
};

struct SortedLevels {
    template <typename Better>
    using Side = SortedSlots<Better>;
};

struct LadderLevels {
    template <typename Better>
    using Side = LadderSlots<Better>;
};

template <typename Levels>
class PooledBook {
public:
    explicit PooledBook(InstrumentId stock_locate = 0) : stock_locate_(stock_locate) {}

    InstrumentId stock_locate() const { return stock_locate_; }

    void add_order(std::uint64_t ref, Side side, Price price, std::uint32_t quantity)
    {
        const std::uint32_t slot = allocate_order();
        const auto make_level = [&] { return allocate_level(price); };
        const std::uint32_t level_slot =
            side == Side::BID ? bids_.find_or_create(price, make_level) : asks_.find_or_create(price, make_level);

        LevelNode& level = levels_[level_slot];
        orders_[slot] = OrderNode{ref, price, quantity, level_slot, level.tail, kNoSlot, side};
        if (level.tail == kNoSlot) {
            level.head = slot;
        } else {
            orders_[level.tail].next = slot;
        }
        level.tail = slot;
        level.quantity += quantity;
        ++level.orders;
        index_.insert(ref, slot);
    }

    void execute_order(std::uint64_t ref, std::uint32_t quantity) { reduce(ref, quantity); }
    void cancel_order(std::uint64_t ref, std::uint32_t quantity) { reduce(ref, quantity); }

    void delete_order(std::uint64_t ref)
    {
        const std::uint32_t slot = index_.find(ref);
        if (slot != kNoSlot) {
            remove_slot(slot);
        }
    }

    void replace_order(std::uint64_t old_ref, std::uint64_t new_ref, Price price, std::uint32_t quantity)
    {
        const std::uint32_t slot = index_.find(old_ref);
        if (slot == kNoSlot) {
            return;
        }
        const Side side = orders_[slot].side;
        remove_slot(slot);
        add_order(new_ref, side, price, quantity);
    }

    std::optional<Price> best_bid() const { return price_of(bids_.best()); }
    std::optional<Price> best_ask() const { return price_of(asks_.best()); }

    std::vector<LevelSummary> bid_depth(std::size_t count) const { return depth(bids_, count); }
    std::vector<LevelSummary> ask_depth(std::size_t count) const { return depth(asks_, count); }

    std::size_t resting_orders() const { return index_.size(); }

private:
    struct OrderNode {
        std::uint64_t ref;
        Price price;
        std::uint32_t quantity;
        std::uint32_t level;
        std::uint32_t prev;
        std::uint32_t next;
        Side side;
    };

    struct LevelNode {
        Price price;
        std::uint64_t quantity;
        std::uint32_t head;
        std::uint32_t tail;
        std::uint32_t orders;
        std::uint32_t next_free;
    };

    void reduce(std::uint64_t ref, std::uint32_t quantity)
    {
        const std::uint32_t slot = index_.find(ref);
        if (slot == kNoSlot) {
            return;
        }
        OrderNode& order = orders_[slot];
        if (quantity >= order.quantity) {
            remove_slot(slot);
            return;
        }
        order.quantity -= quantity;
        levels_[order.level].quantity -= quantity;
    }

    template <typename SideLevels>
    std::vector<LevelSummary> depth(const SideLevels& side, std::size_t count) const
    {
        std::vector<LevelSummary> result;
        side.for_each([&](std::uint32_t level_slot) {
            if (result.size() == count) {
                return false;
            }
            const LevelNode& level = levels_[level_slot];
            result.push_back(LevelSummary{level.price, static_cast<std::uint32_t>(level.quantity)});
            return true;
        });
        return result;
    }

    std::optional<Price> price_of(std::uint32_t level_slot) const
    {
        if (level_slot == kNoSlot) {
            return std::nullopt;
        }
        return levels_[level_slot].price;
    }

    std::uint32_t allocate_order()
    {
        if (free_order_ != kNoSlot) {
            const std::uint32_t slot = free_order_;
            free_order_ = orders_[slot].next;
            return slot;
        }
        orders_.emplace_back();
        return static_cast<std::uint32_t>(orders_.size() - 1);
    }

    std::uint32_t allocate_level(Price price)
    {
        std::uint32_t slot = free_level_;
        if (slot != kNoSlot) {
            free_level_ = levels_[slot].next_free;
        } else {
            levels_.emplace_back();
            slot = static_cast<std::uint32_t>(levels_.size() - 1);
        }
        levels_[slot] = LevelNode{price, 0, kNoSlot, kNoSlot, 0, kNoSlot};
        return slot;
    }

    void remove_slot(std::uint32_t slot)
    {
        const OrderNode order = orders_[slot];
        LevelNode& level = levels_[order.level];
        if (order.prev == kNoSlot) {
            level.head = order.next;
        } else {
            orders_[order.prev].next = order.next;
        }
        if (order.next == kNoSlot) {
            level.tail = order.prev;
        } else {
            orders_[order.next].prev = order.prev;
        }
        level.quantity -= order.quantity;
        if (--level.orders == 0) {
            if (order.side == Side::BID) {
                bids_.erase(order.price);
            } else {
                asks_.erase(order.price);
            }
            level.next_free = free_level_;
            free_level_ = order.level;
        }
        index_.erase(order.ref);
        orders_[slot].next = free_order_;
        free_order_ = slot;
    }

    std::vector<OrderNode> orders_;
    std::vector<LevelNode> levels_;
    std::uint32_t free_order_ = kNoSlot;
    std::uint32_t free_level_ = kNoSlot;
    typename Levels::template Side<std::greater<>> bids_;
    typename Levels::template Side<std::less<>> asks_;
    SlotIndex index_;
    InstrumentId stock_locate_;
};

}  // namespace pooled

using PooledHybridSecurityBook = pooled::PooledBook<pooled::SortedLevels>;
using PooledLadderSecurityBook = pooled::PooledBook<pooled::LadderLevels>;
