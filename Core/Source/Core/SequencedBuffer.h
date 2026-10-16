#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "CircularBuffer.h"

template <Usable T, std::size_t Capacity>
class SequencedBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SequencedBuffer()
    {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    SequencedBuffer(const SequencedBuffer&) = delete;
    SequencedBuffer& operator=(const SequencedBuffer&) = delete;
    SequencedBuffer(SequencedBuffer&&) = delete;
    SequencedBuffer& operator=(SequencedBuffer&&) = delete;
    ~SequencedBuffer() = default;

    bool try_push(T&& value)
    {
        Slot& slot = slots_[tail_ & kMask];
        if (slot.sequence.load(std::memory_order_acquire) != tail_) {
            return false;
        }
        slot.value = std::move(value);
        slot.sequence.store(tail_ + 1, std::memory_order_release);
        ++tail_;
        return true;
    }

    std::optional<T> try_pop()
    {
        Slot& slot = slots_[head_ & kMask];
        if (slot.sequence.load(std::memory_order_acquire) != head_ + 1) {
            return std::nullopt;
        }
        T value = std::move(slot.value);
        slot.sequence.store(head_ + Capacity, std::memory_order_release);
        ++head_;
        return value;
    }

    template <std::invocable<T&> Consume>
    std::size_t pop_batch(Consume&& consume, std::size_t max_items)
    {
        std::size_t count = 0;
        while (count < max_items) {
            Slot& slot = slots_[head_ & kMask];
            if (slot.sequence.load(std::memory_order_acquire) != head_ + 1) {
                break;
            }
            T value = std::move(slot.value);
            slot.sequence.store(head_ + Capacity, std::memory_order_release);
            ++head_;
            ++count;
            consume(value);
        }
        return count;
    }

    bool empty() const
    {
        return slots_[head_ & kMask].sequence.load(std::memory_order_acquire) != head_ + 1;
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    struct Slot {
        std::atomic<std::size_t> sequence;
        T value;
    };

    std::unique_ptr<Slot[]> slots_ = std::make_unique<Slot[]>(Capacity);
    alignas(kCacheLineSize) std::size_t head_ = 0;
    alignas(kCacheLineSize) std::size_t tail_ = 0;
};
