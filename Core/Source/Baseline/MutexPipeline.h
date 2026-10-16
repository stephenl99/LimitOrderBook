#pragma once

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "Core/ThreadPinning.h"

template <typename Message, std::invocable<Message&> Handler>
class MutexPipeline {
public:
    explicit MutexPipeline(Handler handler, std::size_t max_batch = 1,
                           std::optional<std::size_t> consumer_cpu_slot = std::nullopt)
        : max_batch_(std::max<std::size_t>(max_batch, 1)),
          consumer_cpu_slot_(consumer_cpu_slot),
          handler_(std::move(handler)),
          consumer_([this] { consume(); })
    {
    }

    MutexPipeline(const MutexPipeline&) = delete;
    MutexPipeline& operator=(const MutexPipeline&) = delete;
    MutexPipeline(MutexPipeline&&) = delete;
    MutexPipeline& operator=(MutexPipeline&&) = delete;

    ~MutexPipeline() { finish(); }

    void push(Message message)
    {
        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(message));
        }
        ready_.notify_one();
    }

    void finish()
    {
        if (!consumer_.joinable()) {
            return;
        }
        {
            std::lock_guard lock(mutex_);
            done_ = true;
        }
        ready_.notify_all();
        consumer_.join();
    }

private:
    void consume()
    {
        if (consumer_cpu_slot_) {
            pin_current_thread(*consumer_cpu_slot_);
        }
        std::vector<Message> batch;
        batch.reserve(max_batch_);
        while (true) {
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [&] { return !queue_.empty() || done_; });
                if (queue_.empty() && done_) {
                    return;
                }
                while (!queue_.empty() && batch.size() < max_batch_) {
                    batch.push_back(std::move(queue_.front()));
                    queue_.pop();
                }
            }
            for (Message& message : batch) {
                handler_(message);
            }
            batch.clear();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<Message> queue_;
    bool done_ = false;
    std::size_t max_batch_;
    std::optional<std::size_t> consumer_cpu_slot_;
    Handler handler_;
    std::thread consumer_;
};
