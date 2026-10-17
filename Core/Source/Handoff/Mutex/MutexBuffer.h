#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "Handoff/BufferTraits.h"

template <Usable T, std::size_t Capacity>
class MutexBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    MutexBuffer() = default;
    MutexBuffer(const MutexBuffer&) = delete;
    MutexBuffer& operator=(const MutexBuffer&) = delete;
    MutexBuffer(MutexBuffer&&) = delete;
    MutexBuffer& operator=(MutexBuffer&&) = delete;
    ~MutexBuffer() = default;

    bool try_push(T&& value)
    {
        std::lock_guard lock(mutex_);
        if (tail_ - head_ == Capacity) {
            return false;
        }
        slots_[tail_ & kMask] = std::move(value);
        ++tail_;
        return true;
    }

    std::optional<T> try_pop()
    {
        std::lock_guard lock(mutex_);
        if (head_ == tail_) {
            return std::nullopt;
        }
        T value = std::move(slots_[head_ & kMask]);
        ++head_;
        return value;
    }

    template <std::invocable<T&> Consume>
    std::size_t pop_batch(Consume&& consume, std::size_t max_items)
    {
        std::unique_lock lock(mutex_);
        const std::size_t available = tail_ - head_;
        const std::size_t count = available < max_items ? available : max_items;
        const std::size_t head = head_;
        lock.unlock();
        for (std::size_t i = 0; i < count; ++i) {
            consume(slots_[(head + i) & kMask]);
        }
        lock.lock();
        head_ += count;
        return count;
    }

    bool empty() const
    {
        std::lock_guard lock(mutex_);
        return head_ == tail_;
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
    mutable std::mutex mutex_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};
