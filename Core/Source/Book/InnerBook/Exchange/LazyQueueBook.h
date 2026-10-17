#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Book/InnerBook/MarketMaker/AggregateSecurityBook.h"
#include "Book/InnerBook/LevelSummary.h"
#include "Book/InnerBook/Order.h"

struct Fill {
    uint64_t id;
    Price price;
    uint32_t quantity;

    bool operator==(const Fill&) const = default;
};

struct LazyQueueEntry {
    uint64_t sequence;
    uint64_t id;
};

struct LazyQueue {
    static constexpr std::size_t kMinCompact = 64;

    std::vector<LazyQueueEntry> entries;
    std::size_t head = 0;
    std::size_t compact_at = kMinCompact;
};

class HashedQueues {
public:
    LazyQueue& at(Price price) { return queues_[price]; }

    LazyQueue* find(Price price)
    {
        const auto found = queues_.find(price);
        return found == queues_.end() ? nullptr : &found->second;
    }

    void clear() { queues_.clear(); }

private:
    std::unordered_map<Price, LazyQueue> queues_;
};

class SlotQueues {
public:
    void set_tick(Price tick) { tick_ = tick; }

    LazyQueue& at(Price price)
    {
        if (queues_.empty()) {
            if (tick_ <= 0) {
                throw std::logic_error("SlotQueues needs set_tick before the first add");
            }
            base_ = price;
            queues_.resize(1);
            return queues_.front();
        }
        auto index = static_cast<std::ptrdiff_t>((price - base_) / tick_);
        if (index < 0) {
            const auto grow = static_cast<std::size_t>(-index) + slack();
            queues_.insert(queues_.begin(), grow, LazyQueue{});
            base_ -= static_cast<Price>(grow) * tick_;
            index += static_cast<std::ptrdiff_t>(grow);
        } else if (index >= static_cast<std::ptrdiff_t>(queues_.size())) {
            queues_.resize(static_cast<std::size_t>(index) + 1 + slack());
        }
        return queues_[static_cast<std::size_t>(index)];
    }

    LazyQueue* find(Price price)
    {
        if (queues_.empty()) {
            return nullptr;
        }
        const auto index = static_cast<std::ptrdiff_t>((price - base_) / tick_);
        if (index < 0 || index >= static_cast<std::ptrdiff_t>(queues_.size())) {
            return nullptr;
        }
        return &queues_[static_cast<std::size_t>(index)];
    }

    void clear()
    {
        queues_.clear();
        base_ = 0;
    }

private:
    std::size_t slack() const { return queues_.size() / 2 + 16; }

    std::vector<LazyQueue> queues_;
    Price base_ = 0;
    Price tick_ = 0;
};

class HandleSlotQueues {
public:
    void set_tick(Price tick) { tick_ = tick; }

    LazyQueue& at(Price price)
    {
        uint32_t& handle = slots_[static_cast<std::size_t>(slot_for(price))];
        if (handle == kNoQueue) {
            handle = static_cast<uint32_t>(pool_.size());
            pool_.emplace_back();
        }
        return pool_[handle];
    }

    LazyQueue* find(Price price)
    {
        if (slots_.empty()) {
            return nullptr;
        }
        const auto index = static_cast<std::ptrdiff_t>((price - base_) / tick_);
        if (index < 0 || index >= static_cast<std::ptrdiff_t>(slots_.size())) {
            return nullptr;
        }
        const uint32_t handle = slots_[static_cast<std::size_t>(index)];
        return handle == kNoQueue ? nullptr : &pool_[handle];
    }

    void clear()
    {
        slots_.clear();
        pool_.clear();
        base_ = 0;
    }

private:
    static constexpr uint32_t kNoQueue = std::numeric_limits<uint32_t>::max();

    std::ptrdiff_t slot_for(Price price)
    {
        if (slots_.empty()) {
            if (tick_ <= 0) {
                throw std::logic_error("HandleSlotQueues needs set_tick before the first add");
            }
            base_ = price;
            slots_.assign(1, kNoQueue);
            return 0;
        }
        auto index = static_cast<std::ptrdiff_t>((price - base_) / tick_);
        if (index < 0) {
            const auto grow = static_cast<std::size_t>(-index) + slack();
            slots_.insert(slots_.begin(), grow, kNoQueue);
            base_ -= static_cast<Price>(grow) * tick_;
            index += static_cast<std::ptrdiff_t>(grow);
        } else if (index >= static_cast<std::ptrdiff_t>(slots_.size())) {
            slots_.resize(static_cast<std::size_t>(index) + 1 + slack(), kNoQueue);
        }
        return index;
    }

    std::size_t slack() const { return slots_.size() / 2 + 16; }

    std::vector<uint32_t> slots_;
    std::vector<LazyQueue> pool_;
    Price base_ = 0;
    Price tick_ = 0;
};

template <typename Levels, typename Orders, typename Queues>
class LazyQueueBook {
public:
    using BidLevels = typename Levels::template Side<std::greater<>>;
    using AskLevels = typename Levels::template Side<std::less<>>;

    explicit LazyQueueBook(InstrumentId stock_locate = 0) : stock_locate_(stock_locate) {}

    InstrumentId stock_locate() const { return stock_locate_; }

    void set_tick(Price tick)
        requires requires(BidLevels& bids, AskLevels& asks) {
            bids.set_tick(tick);
            asks.set_tick(tick);
        }
    {
        bid_levels_.set_tick(tick);
        ask_levels_.set_tick(tick);
        if constexpr (requires(Queues& queues) { queues.set_tick(tick); }) {
            bid_queues_.set_tick(tick);
            ask_queues_.set_tick(tick);
        }
    }

    void add_order(uint64_t id, Side side, Price price, uint32_t quantity) { add_resting(id, side, price, quantity); }

    void delete_order(uint64_t id)
    {
        if (const auto handle = orders_.find(id)) {
            remove_resting(handle);
        }
    }

    void execute_order(uint64_t id, uint32_t shares) { reduce(id, shares); }
    void cancel_order(uint64_t id, uint32_t shares) { reduce(id, shares); }

    void replace_order(uint64_t old_id, uint64_t new_id, Price price, uint32_t quantity)
    {
        const auto handle = orders_.find(old_id);
        if (!handle) {
            return;
        }
        const Side side = orders_.get(handle).side;
        remove_resting(handle);
        add_resting(new_id, side, price, quantity);
    }

    void modify_order(uint64_t id, Price new_price, uint32_t new_shares)
    {
        const auto handle = orders_.find(id);
        if (!handle) {
            return;
        }
        RestingOrder& order = orders_.get(handle);
        const int64_t quantity_change = static_cast<int64_t>(new_shares) - static_cast<int64_t>(order.quantity);
        const bool keeps_priority = new_price == order.price && new_shares <= order.quantity;
        if (keeps_priority && new_shares > 0) {
            on_side(order.side, [&](auto& levels) { levels.adjust(order.price, quantity_change); });
            order.quantity = new_shares;
            return;
        }
        if (new_shares == 0) {
            remove_resting(handle);
            return;
        }
        on_side(order.side, [&](auto& levels) {
            if (new_price == order.price) {
                levels.adjust(order.price, quantity_change);
            } else {
                levels.remove(order.price, order.quantity);
                levels.add(new_price, new_shares);
            }
        });
        order.price = new_price;
        order.quantity = new_shares;
        order.sequence = ++next_sequence_;
        enqueue(order.side, new_price, order.sequence, id);
    }

    void clear()
    {
        orders_.clear();
        bid_levels_.clear();
        ask_levels_.clear();
        bid_queues_.clear();
        ask_queues_.clear();
    }

    std::size_t resting_orders() const { return orders_.size(); }
    std::size_t bid_level_count() const { return bid_levels_.size(); }
    std::size_t ask_level_count() const { return ask_levels_.size(); }
    std::optional<Price> best_bid() const { return bid_levels_.best(); }
    std::optional<Price> best_ask() const { return ask_levels_.best(); }
    std::vector<LevelSummary> bid_depth(std::size_t levels) const { return bid_levels_.depth(levels); }
    std::vector<LevelSummary> ask_depth(std::size_t levels) const { return ask_levels_.depth(levels); }

    template <typename OnFill>
    uint32_t match(Side resting_side, Price limit, uint32_t quantity, OnFill&& on_fill)
    {
        while (quantity > 0) {
            const std::optional<Price> best = resting_side == Side::BID ? best_bid() : best_ask();
            if (!best || (resting_side == Side::BID ? *best < limit : *best > limit)) {
                break;
            }
            const auto [handle, id] = front(resting_side, *best);
            if (!handle) {
                break;
            }
            RestingOrder& order = orders_.get(handle);
            const uint32_t filled = std::min(quantity, order.quantity);
            on_fill(Fill{id, *best, filled});
            quantity -= filled;
            if (filled == order.quantity) {
                remove_resting(handle);
            } else {
                on_side(resting_side, [&](auto& levels) { levels.adjust(order.price, -static_cast<int64_t>(filled)); });
                order.quantity -= filled;
            }
        }
        return quantity;
    }

    std::vector<std::pair<uint64_t, uint32_t>> queue_at(Side side, Price price)
    {
        std::vector<std::pair<uint64_t, uint32_t>> live;
        const LazyQueue* queue = queues(side).find(price);
        if (queue == nullptr) {
            return live;
        }
        for (std::size_t i = queue->head; i < queue->entries.size(); ++i) {
            const LazyQueueEntry& entry = queue->entries[i];
            if (is_live(entry)) {
                live.emplace_back(entry.id, orders_.get(orders_.find(entry.id)).quantity);
            }
        }
        return live;
    }

private:
    template <typename Action>
    void on_side(Side side, Action&& action)
    {
        if (side == Side::ASK) {
            action(ask_levels_);
        } else {
            action(bid_levels_);
        }
    }

    Queues& queues(Side side) { return side == Side::ASK ? ask_queues_ : bid_queues_; }

    bool is_live(const LazyQueueEntry& entry)
    {
        const auto handle = orders_.find(entry.id);
        return handle && orders_.get(handle).sequence == entry.sequence;
    }

    void enqueue(Side side, Price price, uint64_t sequence, uint64_t id)
    {
        LazyQueue& queue = queues(side).at(price);
        queue.entries.push_back(LazyQueueEntry{sequence, id});
        if (queue.entries.size() - queue.head >= queue.compact_at) {
            compact(queue);
        }
    }

    void compact(LazyQueue& queue)
    {
        std::size_t kept = 0;
        for (std::size_t i = queue.head; i < queue.entries.size(); ++i) {
            const LazyQueueEntry entry = queue.entries[i];
            if (is_live(entry)) {
                queue.entries[kept++] = entry;
            }
        }
        queue.entries.resize(kept);
        queue.head = 0;
        queue.compact_at = std::max(LazyQueue::kMinCompact, 2 * kept);
    }

    std::pair<typename Orders::Handle, uint64_t> front(Side side, Price price)
    {
        LazyQueue* queue = queues(side).find(price);
        if (queue == nullptr) {
            return {typename Orders::Handle{}, 0};
        }
        while (queue->head < queue->entries.size()) {
            const LazyQueueEntry& entry = queue->entries[queue->head];
            const auto handle = orders_.find(entry.id);
            if (handle && orders_.get(handle).sequence == entry.sequence) {
                return {handle, entry.id};
            }
            ++queue->head;
        }
        return {typename Orders::Handle{}, 0};
    }

    void add_resting(uint64_t id, Side side, Price price, uint32_t quantity)
    {
        on_side(side, [&](auto& levels) { levels.add(price, quantity); });
        const uint64_t sequence = ++next_sequence_;
        orders_.insert(id, RestingOrder{price, sequence, quantity, side});
        enqueue(side, price, sequence, id);
    }

    void remove_resting(typename Orders::Handle handle)
    {
        const RestingOrder& order = orders_.get(handle);
        on_side(order.side, [&](auto& levels) { levels.remove(order.price, order.quantity); });
        orders_.erase(handle);
    }

    void reduce(uint64_t id, uint32_t shares)
    {
        const auto handle = orders_.find(id);
        if (!handle) {
            return;
        }
        RestingOrder& order = orders_.get(handle);
        if (shares >= order.quantity) {
            remove_resting(handle);
            return;
        }
        on_side(order.side, [&](auto& levels) { levels.adjust(order.price, -static_cast<int64_t>(shares)); });
        order.quantity -= shares;
    }

    BidLevels bid_levels_;
    AskLevels ask_levels_;
    Orders orders_;
    Queues bid_queues_;
    Queues ask_queues_;
    uint64_t next_sequence_ = 0;
    InstrumentId stock_locate_;
};

using LazyHashLadderSecurityBook = LazyQueueBook<LadderLevels, FlatOrderIndex, HashedQueues>;
using LazySlotLadderSecurityBook = LazyQueueBook<LadderLevels, FlatOrderIndex, SlotQueues>;
using LazyHandleLadderSecurityBook = LazyQueueBook<LadderLevels, FlatOrderIndex, HandleSlotQueues>;
