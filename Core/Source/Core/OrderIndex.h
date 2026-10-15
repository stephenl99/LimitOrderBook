#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Order.h"

struct RestingOrder {
    Price price;
    uint64_t sequence;
    uint32_t quantity;
    Side side;
};

class NodeOrderIndex {
    using Map = std::pmr::unordered_map<uint64_t, RestingOrder>;

public:
    struct Handle {
        Map::iterator position;
        bool found;
        explicit operator bool() const { return found; }
    };

    Handle find(uint64_t id)
    {
        const auto position = orders_.find(id);
        return {position, position != orders_.end()};
    }

    RestingOrder& get(Handle handle) { return handle.position->second; }

    bool insert(uint64_t id, const RestingOrder& order) { return orders_.emplace(id, order).second; }

    void erase(Handle handle) { orders_.erase(handle.position); }

    std::size_t size() const { return orders_.size(); }

    void clear() { orders_.clear(); }

    template <typename Visit>
    void for_each(Visit&& visit) const
    {
        for (const auto& [id, order] : orders_) {
            visit(id, order);
        }
    }

private:
    std::pmr::unsynchronized_pool_resource pool_;
    Map orders_{&pool_};
};

struct MultiplicativeHash {
    static std::size_t home(uint64_t id, unsigned shift, std::size_t) { return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ULL) >> shift); }
};

struct IdentityHash {
    static std::size_t home(uint64_t id, unsigned, std::size_t mask) { return static_cast<std::size_t>(id) & mask; }
};

template <typename Hash>
class BasicFlatOrderIndex {
public:
    struct Handle {
        std::size_t slot;
        bool found;
        explicit operator bool() const { return found; }
    };

    BasicFlatOrderIndex() { allocate(kInitialCapacity); }

    Handle find(uint64_t id) const
    {
        std::size_t slot = home(id);
        while (true) {
            const uint64_t key = slots_[slot].key;
            if (key == id) {
                return {slot, true};
            }
            if (key == kEmpty) {
                return {slot, false};
            }
            slot = (slot + 1) & mask_;
        }
    }

    RestingOrder& get(Handle handle) { return slots_[handle.slot].order; }

    bool insert(uint64_t id, const RestingOrder& order)
    {
        if ((size_ + 1) * 2 > slots_.size()) {
            allocate(slots_.size() * 2);
        }
        const Handle handle = find(id);
        if (handle.found) {
            return false;
        }
        slots_[handle.slot] = Slot{id, order};
        ++size_;
        return true;
    }

    void erase(Handle handle)
    {
        std::size_t hole = handle.slot;
        std::size_t next = (hole + 1) & mask_;
        while (slots_[next].key != kEmpty) {
            const std::size_t wanted = home(slots_[next].key);
            if (((next - wanted) & mask_) >= ((next - hole) & mask_)) {
                slots_[hole] = slots_[next];
                hole = next;
            }
            next = (next + 1) & mask_;
        }
        slots_[hole].key = kEmpty;
        --size_;
    }

    std::size_t size() const { return size_; }

    void clear()
    {
        for (Slot& slot : slots_) {
            slot.key = kEmpty;
        }
        size_ = 0;
    }

    template <typename Visit>
    void for_each(Visit&& visit) const
    {
        for (const Slot& slot : slots_) {
            if (slot.key != kEmpty) {
                visit(slot.key, slot.order);
            }
        }
    }

private:
    static constexpr uint64_t kEmpty = ~uint64_t{0};
    static constexpr std::size_t kInitialCapacity = 1024;

    struct Slot {
        uint64_t key = kEmpty;
        RestingOrder order{};
    };

    std::size_t home(uint64_t id) const { return Hash::home(id, shift_, mask_); }

    void allocate(std::size_t capacity)
    {
        std::vector<Slot> previous = std::exchange(slots_, std::vector<Slot>(capacity));
        mask_ = capacity - 1;
        shift_ = 64 - static_cast<unsigned>(std::countr_zero(capacity));
        for (const Slot& slot : previous) {
            if (slot.key != kEmpty) {
                slots_[find(slot.key).slot] = slot;
            }
        }
    }

    std::vector<Slot> slots_;
    std::size_t mask_ = 0;
    unsigned shift_ = 0;
    std::size_t size_ = 0;
};

using FlatOrderIndex = BasicFlatOrderIndex<MultiplicativeHash>;
using IdentityFlatOrderIndex = BasicFlatOrderIndex<IdentityHash>;
