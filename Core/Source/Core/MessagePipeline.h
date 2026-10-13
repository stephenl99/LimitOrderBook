#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <thread>
#include <type_traits>
#include <utility>

#include "CircularBuffer.h"

template <Usable Message, std::size_t Capacity, std::invocable<Message&> Handler>
class MessagePipeline {
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
            std::this_thread::yield();
        }
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
            if (auto message = queue_.try_pop()) {
                handler_(*message);
            } else if (producer_done_.load(std::memory_order_acquire)) {
                if (queue_.empty()) {
                    break;
                }
            } else {
                std::this_thread::yield();
            }
        }
    }

    CircularBuffer<Message, Capacity> queue_;
    std::atomic<bool> producer_done_ = false;
    Handler handler_;
    std::thread consumer_;
};

template <typename Message, std::size_t Capacity, typename Handler>
MessagePipeline<Message, Capacity, std::decay_t<Handler>> make_pipeline(Handler&& handler)
{
    return MessagePipeline<Message, Capacity, std::decay_t<Handler>>(std::forward<Handler>(handler));
}
