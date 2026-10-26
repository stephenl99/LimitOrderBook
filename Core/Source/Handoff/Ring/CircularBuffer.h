#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "Handoff/BufferTraits.h"

struct AcquireRelease {
    static constexpr std::memory_order relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order acquire = std::memory_order_acquire;
    static constexpr std::memory_order release = std::memory_order_release;
};

struct SequentiallyConsistent {
    static constexpr std::memory_order relaxed = std::memory_order_seq_cst;
    static constexpr std::memory_order acquire = std::memory_order_seq_cst;
    static constexpr std::memory_order release = std::memory_order_seq_cst;
};

template <Usable T, std::size_t Capacity, typename Ordering = AcquireRelease, bool PadIndices = true>
class BasicCircularBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    BasicCircularBuffer() = default;
    BasicCircularBuffer(const BasicCircularBuffer&) = delete;
    BasicCircularBuffer& operator=(const BasicCircularBuffer&) = delete;
    BasicCircularBuffer(BasicCircularBuffer&&) = delete;
    BasicCircularBuffer& operator=(BasicCircularBuffer&&) = delete;
    ~BasicCircularBuffer() = default;

    bool try_push(T&& value)
    {
        const std::size_t tail = tail_.load(Ordering::relaxed);
        if (tail - head_cache_ == Capacity) {
            head_cache_ = head_.load(Ordering::acquire);
            if (tail - head_cache_ == Capacity) {
                return false;
            }
        }
        slots_[tail & kMask] = std::move(value);
        tail_.store(tail + 1, Ordering::release);
        return true;
    }

    std::optional<T> try_pop()
    {
        const std::size_t head = head_.load(Ordering::relaxed);
        if (head == tail_cache_) {
            tail_cache_ = tail_.load(Ordering::acquire);
            if (head == tail_cache_) {
                return std::nullopt;
            }
        }
        T value = std::move(slots_[head & kMask]);
        head_.store(head + 1, Ordering::release);
        return value;
    }

    template <std::invocable<T&> Consume>
    std::size_t pop_batch(Consume&& consume, std::size_t max_items)
    {
        const std::size_t head = head_.load(Ordering::relaxed);
        if (tail_cache_ - head < max_items) {
            tail_cache_ = tail_.load(Ordering::acquire);
        }
        const std::size_t available = tail_cache_ - head;
        const std::size_t count = available < max_items ? available : max_items;
        for (std::size_t i = 0; i < count; ++i) {
            T value = std::move(slots_[(head + i) & kMask]);
            consume(value);
        }
        if (count != 0) {
            head_.store(head + count, Ordering::release);
        }
        return count;
    }

    bool empty() const
    {
        return head_.load(Ordering::acquire) == tail_.load(Ordering::acquire);
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kIndexAlignment = PadIndices ? kCacheLineSize : alignof(std::atomic<std::size_t>);

    std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
    alignas(kIndexAlignment) std::atomic<std::size_t> head_{0}; // needed because of false sharing between head and tail.
    std::size_t tail_cache_ = 0;
    alignas(kIndexAlignment) std::atomic<std::size_t> tail_{0};
    std::size_t head_cache_ = 0;
};

template <Usable T, std::size_t Capacity>
using CircularBuffer = BasicCircularBuffer<T, Capacity>;

template <Usable T, std::size_t Capacity>
using UnpaddedCircularBuffer = BasicCircularBuffer<T, Capacity, AcquireRelease, false>;

template <Usable T, std::size_t Capacity>
using SeqCstUnpaddedCircularBuffer = BasicCircularBuffer<T, Capacity, SequentiallyConsistent, false>;
