#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <thread>
#include <type_traits>
#include <utility>

#include "CircularBuffer.h"
#include "IdleStrategy.h"
// invocable to make sure the handler can accept the message as an arg
template <Usable Message, std::size_t Capacity, std::invocable<Message&> Handler, IdleStrategy Idle = YieldWhenIdle,
          std::size_t MaxBatch = 1>
class MessagePipeline {
    static_assert(MaxBatch >= 1 && MaxBatch <= Capacity, "MaxBatch must be between 1 and Capacity");

public:
    explicit MessagePipeline(Handler handler)
        : handler_(std::move(handler)), consumer_([this] { consume(); })
    {
    }

    MessagePipeline(const MessagePipeline&) = delete;
    MessagePipeline& operator=(const MessagePipeline&) = delete;

    ~MessagePipeline() { finish(); }

    void push(Message message)
    {
        while (!queue_.try_push(std::move(message))) {
            producer_idle_.wait();
        }
        producer_idle_.reset();
    }

    void finish()
    {
        if (!consumer_.joinable()) {
            return;
        }
        producer_done_.store(true, std::memory_order_release);
        consumer_.join();
    }

private:
    void consume()
    {
        while (true) {
            if (take()) {
                consumer_idle_.reset();
            } else if (producer_done_.load(std::memory_order_acquire)) {
                if (queue_.empty()) {
                    break;
                }
            } else {
                consumer_idle_.wait();
            }
        }
    }

    bool take()
    {
        if constexpr (MaxBatch == 1) {
            if (auto message = queue_.try_pop()) {
                handler_(*message);
                return true;
            }
            return false;
        } else {
            return queue_.pop_batch(handler_, MaxBatch) != 0;
        }
    }

    CircularBuffer<Message, Capacity> queue_;
    alignas(kCacheLineSize) Idle producer_idle_;
    alignas(kCacheLineSize) Idle consumer_idle_;
    std::atomic<bool> producer_done_ = false;
    Handler handler_;
    std::thread consumer_;
};

template <typename Message, std::size_t Capacity, typename Idle = YieldWhenIdle, std::size_t MaxBatch = 1, typename Handler>
MessagePipeline<Message, Capacity, std::decay_t<Handler>, Idle, MaxBatch> make_pipeline(Handler&& handler)
{
    return MessagePipeline<Message, Capacity, std::decay_t<Handler>, Idle, MaxBatch>(std::forward<Handler>(handler));
}
