#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

#include "Baseline/MutexDecoder.h"
#include "Baseline/SingleThreadDecoder.h"
#include "Core/CircularBuffer.h"
#include "Core/Decoder.h"

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kRingCapacity = 1 << 16;

void prefer_performance_cores()
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

void cpu_pause()
{
#if defined(__aarch64__)
    __builtin_arm_yield();
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

struct YieldWhenIdle {
    static void wait() { std::this_thread::yield(); }
};

struct SpinWhenIdle {
    static void wait() { cpu_pause(); }
};

std::int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

double elapsed_ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::uint64_t count_frames(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::uint64_t frames = 0;
    uint8_t len_buf[2]{};
    while (input.read(reinterpret_cast<char*>(len_buf), 2)) {
        const auto len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (len == 0 || !input.seekg(len, std::ios::cur)) {
            break;
        }
        ++frames;
    }
    return frames;
}

class MutexQueue {
public:
    void push(std::int64_t value)
    {
        {
            std::lock_guard lock(mtx_);
            q_.push(value);
        }
        cv_.notify_one();
    }

    void close()
    {
        {
            std::lock_guard lock(mtx_);
            done_ = true;
        }
        cv_.notify_all();
    }

    bool pop(std::int64_t& value)
    {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [&] { return !q_.empty() || done_; });
        if (q_.empty()) {
            return false;
        }
        value = q_.front();
        q_.pop();
        return true;
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::int64_t> q_;
    bool done_ = false;
};

template <typename Idle>
class RingQueue {
public:
    void push(std::int64_t value)
    {
        while (!ring_.try_push(std::move(value))) {
            Idle::wait();
        }
    }

    void close() { done_.store(true, std::memory_order_release); }

    bool pop(std::int64_t& value)
    {
        while (true) {
            if (auto item = ring_.try_pop()) {
                value = *item;
                return true;
            }
            if (done_.load(std::memory_order_acquire)) {
                if (ring_.empty()) {
                    return false;
                }
            } else {
                Idle::wait();
            }
        }
    }

private:
    CircularBuffer<std::int64_t, kRingCapacity> ring_;
    std::atomic<bool> done_ = false;
};

template <typename Queue>
std::int64_t transfer(std::int64_t items)
{
    Queue queue;
    std::int64_t sum = 0;

    std::thread producer([&] {
        prefer_performance_cores();
        for (std::int64_t i = 0; i < items; ++i) {
            queue.push(i);
        }
        queue.close();
    });

    std::int64_t value;
    while (queue.pop(value)) {
        sum += value;
    }
    producer.join();
    return sum;
}

template <typename Queue>
std::vector<std::int64_t> paced_latencies(std::int64_t items, std::int64_t interval_ns)
{
    Queue queue;
    std::vector<std::int64_t> latencies;
    latencies.reserve(items);

    std::thread producer([&] {
        prefer_performance_cores();
        const std::int64_t start = now_ns() + 1'000'000;
        for (std::int64_t i = 0; i < items; ++i) {
            const std::int64_t scheduled = start + i * interval_ns;
            while (now_ns() < scheduled) {
            }
            queue.push(scheduled);
        }
        queue.close();
    });

    std::int64_t scheduled;
    while (queue.pop(scheduled)) {
        latencies.push_back(now_ns() - scheduled);
    }
    producer.join();
    std::sort(latencies.begin(), latencies.end());
    return latencies;
}

const std::array<std::string, 3> kQueueImpls = {"mutex", "ring", "ring_spin"};

std::int64_t transfer_with(const std::string& impl, std::int64_t items)
{
    if (impl == "mutex") {
        return transfer<MutexQueue>(items);
    }
    if (impl == "ring") {
        return transfer<RingQueue<YieldWhenIdle>>(items);
    }
    return transfer<RingQueue<SpinWhenIdle>>(items);
}

std::vector<std::int64_t> paced_latencies_with(const std::string& impl, std::int64_t items, std::int64_t interval_ns)
{
    if (impl == "mutex") {
        return paced_latencies<MutexQueue>(items, interval_ns);
    }
    if (impl == "ring") {
        return paced_latencies<RingQueue<YieldWhenIdle>>(items, interval_ns);
    }
    return paced_latencies<RingQueue<SpinWhenIdle>>(items, interval_ns);
}

std::int64_t percentile(const std::vector<std::int64_t>& sorted, double p)
{
    const auto index = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

int run_queue(std::int64_t items, int runs)
{
    const std::int64_t expected = items * (items - 1) / 2;
    for (const std::string& impl : kQueueImpls) {
        transfer_with(impl, items / 10);
    }

    for (int run = 0; run < runs; ++run) {
        for (std::size_t k = 0; k < kQueueImpls.size(); ++k) {
            const std::string& impl = kQueueImpls[(run + k) % kQueueImpls.size()];
            const auto start = Clock::now();
            const std::int64_t sum = transfer_with(impl, items);
            const double ms = elapsed_ms(start);
            if (sum != expected) {
                std::cerr << "queue_bench: " << impl << " lost items (sum " << sum << " != " << expected << ")\n";
                return 1;
            }
            std::cout << "queue," << impl << ',' << items << ',' << run << ',' << ms << '\n';
        }
    }
    return 0;
}

int run_latency(std::int64_t rate_per_sec, double seconds, int runs)
{
    const std::int64_t interval_ns = 1'000'000'000 / rate_per_sec;
    const auto items = static_cast<std::int64_t>(static_cast<double>(rate_per_sec) * seconds);
    for (const std::string& impl : kQueueImpls) {
        paced_latencies_with(impl, items / 10, interval_ns);
    }

    for (int run = 0; run < runs; ++run) {
        for (std::size_t k = 0; k < kQueueImpls.size(); ++k) {
            const std::string& impl = kQueueImpls[(run + k) % kQueueImpls.size()];
            const auto sorted = paced_latencies_with(impl, items, interval_ns);
            std::cout << "latency," << impl << ',' << rate_per_sec << ',' << run << ','
                      << percentile(sorted, 0.50) << ',' << percentile(sorted, 0.99) << ','
                      << percentile(sorted, 0.999) << ',' << sorted.back() << '\n';
        }
    }
    return 0;
}

int run_decode(const fs::path& path, int runs)
{
    const std::uint64_t frames = count_frames(path);
    if (frames == 0) {
        std::cerr << "queue_bench: cannot read " << path << '\n';
        return 1;
    }

    auto decode = [&](const std::string& impl) {
        if (impl == "single") {
            return SingleThreadDecoder::decode_file(path);
        }
        return impl == "mutex" ? MutexDecoder::decode_file(path) : Decoder::decode_file(path);
    };

    const std::array<std::string, 3> impls = {"single", "mutex", "ring"};
    for (const std::string& impl : impls) {
        const auto warmup = decode(impl);
        if (!warmup) {
            std::cerr << "queue_bench: " << warmup.error() << '\n';
            return 1;
        }
    }

    for (int run = 0; run < runs; ++run) {
        for (std::size_t k = 0; k < impls.size(); ++k) {
            const std::string& impl = impls[(run + k) % impls.size()];
            const auto start = Clock::now();
            const auto result = decode(impl);
            const double ms = elapsed_ms(start);
            if (!result) {
                std::cerr << "queue_bench: " << result.error() << '\n';
                return 1;
            }
            std::cout << "decode," << impl << ',' << frames << ',' << run << ',' << ms << '\n';
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cerr << "usage: queue_bench queue <items> <runs>\n"
                  << "       queue_bench latency <msgs_per_sec> <runs> [seconds]\n"
                  << "       queue_bench decode <file.bin> <runs>\n";
        return 1;
    }

    prefer_performance_cores();
    const std::string mode = argv[1];
    const int runs = std::atoi(argv[3]);
    if (mode == "queue") {
        return run_queue(std::atoll(argv[2]), runs);
    }
    if (mode == "latency") {
        return run_latency(std::atoll(argv[2]), argc > 4 ? std::atof(argv[4]) : 2.0, runs);
    }
    if (mode == "decode") {
        return run_decode(argv[2], runs);
    }
    std::cerr << "queue_bench: unknown mode " << mode << '\n';
    return 1;
}
