#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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
#include "Core/IdleStrategy.h"
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

double thread_cpu_ns()
{
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec);
}

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
            push_idle_.wait();
        }
        push_idle_.reset();
    }

    void close() { done_.store(true, std::memory_order_release); }

    bool pop(std::int64_t& value)
    {
        while (true) {
            if (auto item = ring_.try_pop()) {
                pop_idle_.reset();
                value = *item;
                return true;
            }
            if (done_.load(std::memory_order_acquire)) {
                if (ring_.empty()) {
                    return false;
                }
            } else {
                pop_idle_.wait();
            }
        }
    }

private:
    CircularBuffer<std::int64_t, kRingCapacity> ring_;
    std::atomic<bool> done_ = false;
    Idle push_idle_;
    Idle pop_idle_;
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

struct PacedResult {
    std::vector<std::int64_t> sorted;
    double consumer_cpu_pct;
};

template <typename Queue>
PacedResult paced_latencies(std::int64_t items, std::int64_t interval_ns)
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

    const auto wall_start = Clock::now();
    const double cpu_start = thread_cpu_ns();
    std::int64_t scheduled;
    while (queue.pop(scheduled)) {
        latencies.push_back(now_ns() - scheduled);
    }
    const double cpu_ns = thread_cpu_ns() - cpu_start;
    const double wall_ns = std::chrono::duration<double, std::nano>(Clock::now() - wall_start).count();
    producer.join();
    std::sort(latencies.begin(), latencies.end());
    return {std::move(latencies), 100.0 * cpu_ns / wall_ns};
}

const std::array<std::string, 4> kQueueImpls = {"mutex", "ring", "ring_spin", "ring_backoff"};

std::int64_t transfer_with(const std::string& impl, std::int64_t items)
{
    if (impl == "mutex") {
        return transfer<MutexQueue>(items);
    }
    if (impl == "ring") {
        return transfer<RingQueue<YieldWhenIdle>>(items);
    }
    if (impl == "ring_spin") {
        return transfer<RingQueue<SpinWhenIdle>>(items);
    }
    return transfer<RingQueue<BackoffWhenIdle>>(items);
}

PacedResult paced_latencies_with(const std::string& impl, std::int64_t items, std::int64_t interval_ns)
{
    if (impl == "mutex") {
        return paced_latencies<MutexQueue>(items, interval_ns);
    }
    if (impl == "ring") {
        return paced_latencies<RingQueue<YieldWhenIdle>>(items, interval_ns);
    }
    if (impl == "ring_spin") {
        return paced_latencies<RingQueue<SpinWhenIdle>>(items, interval_ns);
    }
    return paced_latencies<RingQueue<BackoffWhenIdle>>(items, interval_ns);
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
            const auto result = paced_latencies_with(impl, items, interval_ns);
            std::cout << "latency," << impl << ',' << rate_per_sec << ',' << run << ','
                      << percentile(result.sorted, 0.50) << ',' << percentile(result.sorted, 0.99) << ','
                      << percentile(result.sorted, 0.999) << ',' << result.sorted.back() << ','
                      << result.consumer_cpu_pct << '\n';
        }
    }
    return 0;
}

constexpr const char* pause_name(PauseKind kind)
{
    return kind == PauseKind::Isb ? "isb" : "yield";
}

double sweep_seconds(std::int64_t rate)
{
    if (rate >= 100'000) {
        return 0.5;
    }
    return rate >= 10'000 ? 1.0 : 2.0;
}

template <typename Idle>
void sweep_one(const char* policy, const char* pause, std::int64_t spin, std::int64_t yield, int runs)
{
    using Queue = RingQueue<Idle>;
    constexpr std::int64_t kFlatItems = 5'000'000;
    const std::array<std::int64_t, 4> rates = {1'000'000, 100'000, 10'000, 1'000};
    const auto prefix = [&] {
        std::cout << policy << ',' << pause << ',' << spin << ',' << yield << ',';
    };

    for (int run = 0; run < runs; ++run) {
        const auto start = Clock::now();
        transfer<Queue>(kFlatItems);
        const double ms = elapsed_ms(start);
        prefix();
        std::cout << "flat," << run << ',' << static_cast<double>(kFlatItems) / ms / 1000.0 << ",,,,\n";
    }
    for (const std::int64_t rate : rates) {
        const auto items = static_cast<std::int64_t>(static_cast<double>(rate) * sweep_seconds(rate));
        for (int run = 0; run < runs; ++run) {
            const auto result = paced_latencies<Queue>(items, 1'000'000'000 / rate);
            prefix();
            std::cout << rate << ',' << run << ",," << percentile(result.sorted, 0.50) << ','
                      << percentile(result.sorted, 0.99) << ',' << percentile(result.sorted, 0.999) << ','
                      << result.consumer_cpu_pct << '\n';
        }
    }
    std::cout.flush();
}

template <PauseKind Kind, std::uint32_t Yield, std::uint32_t... Spins>
void sweep_row(int runs)
{
    (sweep_one<BasicBackoff<Spins, Yield, Kind>>("backoff", pause_name(Kind), Spins, Yield, runs), ...);
}

int run_sweep(int runs)
{
    std::cout << "policy,pause,spin,yield,scenario,run,throughput_mps,p50_ns,p99_ns,p999_ns,consumer_cpu_pct\n";
    sweep_one<YieldWhenIdle>("yield", "none", 0, -1, runs);
    sweep_one<BasicSpin<PauseKind::Yield>>("spin", "yield", -1, 0, runs);
    sweep_one<BasicSpin<PauseKind::Isb>>("spin", "isb", -1, 0, runs);
    sweep_row<PauseKind::Yield, 0, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<PauseKind::Yield, 64, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<PauseKind::Yield, 2000, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<PauseKind::Yield, 20000, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<PauseKind::Isb, 2000, 32, 256, 2000>(runs);
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
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "sweep" && argc > 2) {
        prefer_performance_cores();
        return run_sweep(std::atoi(argv[2]));
    }
    if (argc < 4) {
        std::cerr << "usage: queue_bench queue <items> <runs>\n"
                  << "       queue_bench latency <msgs_per_sec> <runs> [seconds]\n"
                  << "       queue_bench decode <file.bin> <runs>\n"
                  << "       queue_bench sweep <runs>\n";
        return 1;
    }

    prefer_performance_cores();
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
