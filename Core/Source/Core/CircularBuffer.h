#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

inline constexpr std::size_t kCacheLineSize = 128;

template <typename T>
concept Usable = std::movable<T> && std::default_initializable<T>;

template <Usable T, std::size_t Capacity>
class CircularBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    CircularBuffer() = default;
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;
    CircularBuffer(CircularBuffer&&) = delete;
    CircularBuffer& operator=(CircularBuffer&&) = delete;
    ~CircularBuffer() = default;

    bool try_push(T&& value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_cache_ == Capacity) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail - head_cache_ == Capacity) {
                return false;
            }
        }
        slots_[tail & kMask] = std::move(value);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> try_pop()
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cache_) {
                return std::nullopt;
            }
        }
        T value = std::move(slots_[head & kMask]);
        head_.store(head + 1, std::memory_order_release);
        return value;
    }

    template <std::invocable<T&> Consume>
    std::size_t pop_batch(Consume&& consume, std::size_t max_items)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (tail_cache_ - head < max_items) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
        }
        const std::size_t available = tail_cache_ - head;
        const std::size_t count = available < max_items ? available : max_items;
        for (std::size_t i = 0; i < count; ++i) {
            T value = std::move(slots_[(head + i) & kMask]);
            consume(value);
        }
        if (count != 0) {
            head_.store(head + count, std::memory_order_release);
        }
        return count;
    }

    bool empty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kCacheLine = kCacheLineSize;

    std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; // needed because of false sharing between head and tail.
    std::size_t tail_cache_ = 0;
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    std::size_t head_cache_ = 0;
};
