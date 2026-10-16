#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include "CircularBuffer.h"
#include "IdleStrategy.h"
#include "ThreadPinning.h"
// invocable to make sure the handler can accept the message as an arg
template <Usable Message, std::size_t Capacity, std::invocable<Message&> Handler, IdleStrategy Idle = YieldWhenIdle,
          template <Usable, std::size_t> class Buffer = CircularBuffer>
class MessagePipeline {
public:
    explicit MessagePipeline(Handler handler, std::size_t max_batch = 1,
                             std::optional<std::size_t> consumer_cpu_slot = std::nullopt)
        : max_batch_(std::clamp<std::size_t>(max_batch, 1, Capacity)),
          consumer_cpu_slot_(consumer_cpu_slot),
          handler_(std::move(handler)),
          consumer_([this] { consume(); })
    {
    }

    MessagePipeline(const MessagePipeline&) = delete;
    MessagePipeline& operator=(const MessagePipeline&) = delete;
    MessagePipeline(MessagePipeline&&) = delete;
    MessagePipeline& operator=(MessagePipeline&&) = delete;

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
        if (consumer_cpu_slot_) {
            pin_current_thread(*consumer_cpu_slot_);
        }
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
        if (max_batch_ == 1) {
            if (auto message = queue_.try_pop()) {
                handler_(*message);
                return true;
            }
            return false;
        }
        return queue_.pop_batch(handler_, max_batch_) != 0;
    }

    Buffer<Message, Capacity> queue_;
    alignas(kCacheLineSize) Idle producer_idle_;
    alignas(kCacheLineSize) Idle consumer_idle_;
    std::atomic<bool> producer_done_ = false;
    std::size_t max_batch_;
    std::optional<std::size_t> consumer_cpu_slot_;
    Handler handler_;
    std::thread consumer_;
};

template <typename Message, std::size_t Capacity, typename Idle = YieldWhenIdle, typename Handler>
MessagePipeline<Message, Capacity, std::decay_t<Handler>, Idle> make_pipeline(Handler&& handler, std::size_t max_batch = 1)
{
    return MessagePipeline<Message, Capacity, std::decay_t<Handler>, Idle>(std::forward<Handler>(handler), max_batch);
}
