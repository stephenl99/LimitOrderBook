#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

template <std::movable T, std::size_t Capacity>
    requires std::default_initializable<T>
class CircularBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    CircularBuffer() = default;
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;

    bool try_push(T&& value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        slots_[tail & kMask] = std::move(value);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> try_pop()
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T value = std::move(slots_[head & kMask]);
        head_.store(head + 1, std::memory_order_release);
        return value;
    }

    bool empty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kCacheLine = 128;

    std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
};
