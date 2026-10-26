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


#include "Core/Decoder/ItchDecoder.h"
#include "Core/Decoder/ItchMutexDecoder.h"
#include "Core/Decoder/ItchShardedDecoder.h"
#include "Core/Decoder/ItchSingleThreadDecoder.h"
#include "Core/MappedFile.h"
#include "Core/ThreadPinning.h"
#include "Handoff/IdleStrategy.h"
#include "Handoff/MessagePipeline.h"
#include "Handoff/Ring/CircularBuffer.h"
#include "Handoff/Sequenced/SequencedBuffer.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/HandleMessage.h"

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kRingCapacity = 1 << 16;

enum class ThreadRole : std::size_t {
    Producer = 0,
    Consumer = 1,
};

void prefer_performance_cores(ThreadRole role = ThreadRole::Consumer)
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    pin_current_thread(static_cast<std::size_t>(role));
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

template <typename T, std::size_t Capacity>
class UncachedIndexBuffer {
public:
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

    bool empty() const { return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire); }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
};

template <typename Idle, typename Buffer = CircularBuffer<std::int64_t, kRingCapacity>>
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
    Buffer ring_;
    alignas(kCacheLineSize) Idle push_idle_;
    alignas(kCacheLineSize) Idle pop_idle_;
    std::atomic<bool> done_ = false;
};

template <typename Idle, std::size_t Batch>
class BatchRingQueue {
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
            if (cursor_ < count_) {
                value = buffer_[cursor_++];
                return true;
            }
            cursor_ = 0;
            count_ = 0;
            if (ring_.pop_batch([this](std::int64_t& item) { buffer_[count_++] = item; }, Batch) != 0) {
                pop_idle_.reset();
                continue;
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
    alignas(kCacheLineSize) Idle push_idle_;
    alignas(kCacheLineSize) Idle pop_idle_;
    std::atomic<bool> done_ = false;
    std::array<std::int64_t, Batch> buffer_{};
    std::size_t cursor_ = 0;
    std::size_t count_ = 0;
};

template <typename T, std::size_t Capacity>
class MpmcQueue {
public:
    MpmcQueue()
    {
        for (std::size_t i = 0; i < Capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool try_push(T value)
    {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = cells_[pos & kMask];
            const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.value = value;
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    std::optional<T> try_pop()
    {
        T value;
        if (try_pop(value)) {
            return value;
        }
        return std::nullopt;
    }

    bool empty() const
    {
        const std::size_t pos = dequeue_pos_.load(std::memory_order_acquire);
        return cells_[pos & kMask].sequence.load(std::memory_order_acquire) != pos + 1;
    }

    bool try_pop(T& value)
    {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = cells_[pos & kMask];
            const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    value = cell.value;
                    cell.sequence.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    struct Cell {
        std::atomic<std::size_t> sequence;
        T value;
    };

    std::unique_ptr<Cell[]> cells_ = std::make_unique<Cell[]>(Capacity);
    alignas(128) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(128) std::atomic<std::size_t> dequeue_pos_{0};
};

template <typename Queue>
std::int64_t transfer(std::int64_t items)
{
    Queue queue;
    std::int64_t sum = 0;

    std::thread producer([&] {
        prefer_performance_cores(ThreadRole::Producer);
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
        prefer_performance_cores(ThreadRole::Producer);
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

const std::array<std::string, 3> kBatchImpls = {"backoff", "backoff_batch16", "backoff_batch256"};

std::int64_t transfer_batched(const std::string& impl, std::int64_t items)
{
    if (impl == "backoff") {
        return transfer<RingQueue<BackoffWhenIdle>>(items);
    }
    if (impl == "backoff_batch16") {
        return transfer<BatchRingQueue<BackoffWhenIdle, 16>>(items);
    }
    return transfer<BatchRingQueue<BackoffWhenIdle, 256>>(items);
}

PacedResult paced_batched(const std::string& impl, std::int64_t items, std::int64_t interval_ns)
{
    if (impl == "backoff") {
        return paced_latencies<RingQueue<BackoffWhenIdle>>(items, interval_ns);
    }
    if (impl == "backoff_batch16") {
        return paced_latencies<BatchRingQueue<BackoffWhenIdle, 16>>(items, interval_ns);
    }
    return paced_latencies<BatchRingQueue<BackoffWhenIdle, 256>>(items, interval_ns);
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

const std::array<std::string, 4> kVariantImpls = {"uncached_index", "ring", "sequenced", "mpmc"};

template <template <typename> class Run>
auto with_variant(const std::string& impl)
{
    using Seq = SequencedBuffer<std::int64_t, kRingCapacity>;
    using Uncached = UncachedIndexBuffer<std::int64_t, kRingCapacity>;
    using Mpmc = MpmcQueue<std::int64_t, kRingCapacity>;
    if (impl == "uncached_index") {
        return Run<RingQueue<BackoffWhenIdle, Uncached>>{}();
    }
    if (impl == "sequenced") {
        return Run<RingQueue<BackoffWhenIdle, Seq>>{}();
    }
    if (impl == "mpmc") {
        return Run<RingQueue<BackoffWhenIdle, Mpmc>>{}();
    }
    return Run<RingQueue<BackoffWhenIdle>>{}();
}

std::int64_t g_items = 0;
std::int64_t g_interval_ns = 0;

template <typename Queue>
struct FlatRun {
    std::int64_t operator()() const { return transfer<Queue>(g_items); }
};

template <typename Queue>
struct PacedRun {
    PacedResult operator()() const { return paced_latencies<Queue>(g_items, g_interval_ns); }
};

int run_variants(int runs)
{
    constexpr std::int64_t kItems = 10'000'000;
    g_items = kItems;
    for (int run = 0; run < runs; ++run) {
        for (std::size_t k = 0; k < kVariantImpls.size(); ++k) {
            const std::string& impl = kVariantImpls[(run + k) % kVariantImpls.size()];
            const auto start = Clock::now();
            const std::int64_t sum = with_variant<FlatRun>(impl);
            const double ms = elapsed_ms(start);
            if (sum != kItems * (kItems - 1) / 2) {
                std::cerr << "queue_bench: " << impl << " lost items\n";
                return 1;
            }
            std::cout << "variant," << impl << ",flat," << run << ',' << static_cast<double>(kItems) / ms / 1000.0
                      << ",,,,\n";
        }
    }
    const std::array<std::int64_t, 3> rates = {1'000'000, 100'000, 10'000};
    for (const std::int64_t rate : rates) {
        g_items = static_cast<std::int64_t>(static_cast<double>(rate) * (rate >= 100'000 ? 0.5 : 1.0));
        g_interval_ns = 1'000'000'000 / rate;
        for (int run = 0; run < runs; ++run) {
            for (std::size_t k = 0; k < kVariantImpls.size(); ++k) {
                const std::string& impl = kVariantImpls[(run + k) % kVariantImpls.size()];
                const auto result = with_variant<PacedRun>(impl);
                std::cout << "variant," << impl << ',' << rate << ',' << run << ",," << percentile(result.sorted, 0.50)
                          << ',' << percentile(result.sorted, 0.99) << ',' << percentile(result.sorted, 0.999) << ','
                          << result.consumer_cpu_pct << '\n';
            }
        }
    }
    return 0;
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

double sweep_seconds(std::int64_t rate)
{
    if (rate >= 100'000) {
        return 0.5;
    }
    return rate >= 10'000 ? 1.0 : 2.0;
}

template <typename Idle>
void sweep_one(const char* policy, std::int64_t spin, std::int64_t yield, int runs)
{
    using Queue = RingQueue<Idle>;
    constexpr std::int64_t kFlatItems = 5'000'000;
    const std::array<std::int64_t, 4> rates = {1'000'000, 100'000, 10'000, 1'000};
    const auto prefix = [&] {
        std::cout << policy << ',' << spin << ',' << yield << ',';
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

template <std::uint32_t Yield, std::uint32_t... Spins>
void sweep_row(int runs)
{
    (sweep_one<BasicBackoff<Spins, Yield>>("backoff", Spins, Yield, runs), ...);
}

int run_sweep(int runs)
{
    std::cout << "policy,spin,yield,scenario,run,throughput_mps,p50_ns,p99_ns,p999_ns,consumer_cpu_pct\n";
    sweep_one<YieldWhenIdle>("yield", 0, -1, runs);
    sweep_one<SpinWhenIdle>("spin", -1, 0, runs);
    sweep_row<0, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<64, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<2000, 0, 32, 256, 2000, 16000>(runs);
    sweep_row<20000, 0, 32, 256, 2000, 16000>(runs);
    return 0;
}

int run_batching(int runs)
{
    constexpr std::int64_t kItems = 5'000'000;
    for (int run = 0; run < runs; ++run) {
        for (std::size_t k = 0; k < kBatchImpls.size(); ++k) {
            const std::string& impl = kBatchImpls[(run + k) % kBatchImpls.size()];
            const auto start = Clock::now();
            const std::int64_t sum = transfer_batched(impl, kItems);
            const double ms = elapsed_ms(start);
            if (sum != kItems * (kItems - 1) / 2) {
                std::cerr << "queue_bench: " << impl << " lost items\n";
                return 1;
            }
            std::cout << "batch," << impl << ",flat," << run << ',' << static_cast<double>(kItems) / ms / 1000.0
                      << ",,,,\n";
        }
    }
    const std::array<std::int64_t, 3> rates = {1'000'000, 100'000, 10'000};
    for (const std::int64_t rate : rates) {
        const auto items = static_cast<std::int64_t>(static_cast<double>(rate) * (rate >= 100'000 ? 0.5 : 1.0));
        for (int run = 0; run < runs; ++run) {
            for (std::size_t k = 0; k < kBatchImpls.size(); ++k) {
                const std::string& impl = kBatchImpls[(run + k) % kBatchImpls.size()];
                const auto result = paced_batched(impl, items, 1'000'000'000 / rate);
                std::cout << "batch," << impl << ',' << rate << ',' << run << ",," << percentile(result.sorted, 0.50)
                          << ',' << percentile(result.sorted, 0.99) << ',' << percentile(result.sorted, 0.999) << ','
                          << result.consumer_cpu_pct << '\n';
            }
        }
    }
    return 0;
}

template <typename Buffer>
double fanout_sharded(std::int64_t items, std::size_t consumers)
{
    std::vector<std::unique_ptr<RingQueue<BackoffWhenIdle, Buffer>>> rings;
    for (std::size_t c = 0; c < consumers; ++c) {
        rings.push_back(std::make_unique<RingQueue<BackoffWhenIdle, Buffer>>());
    }
    std::vector<std::int64_t> sums(consumers, 0);
    const auto start = Clock::now();
    std::vector<std::thread> threads;
    for (std::size_t c = 0; c < consumers; ++c) {
        threads.emplace_back([&, c] {
            pin_current_thread(1 + c);
            std::int64_t sum = 0;
            std::int64_t value;
            while (rings[c]->pop(value)) {
                sum += value;
            }
            sums[c] = sum;
        });
    }
    for (std::int64_t i = 0; i < items; ++i) {
        rings[static_cast<std::size_t>(i) % consumers]->push(i);
    }
    for (auto& ring : rings) {
        ring->close();
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const double ms = elapsed_ms(start);
    std::int64_t total = 0;
    for (const std::int64_t sum : sums) {
        total += sum;
    }
    return total == items * (items - 1) / 2 ? static_cast<double>(items) / ms / 1000.0 : -1.0;
}

double fanout_mpmc(std::int64_t items, std::size_t consumers)
{
    MpmcQueue<std::int64_t, kRingCapacity> queue;
    std::atomic<bool> done = false;
    std::vector<std::int64_t> sums(consumers, 0);
    const auto start = Clock::now();
    std::vector<std::thread> threads;
    for (std::size_t c = 0; c < consumers; ++c) {
        threads.emplace_back([&, c] {
            pin_current_thread(1 + c);
            BackoffWhenIdle idle;
            std::int64_t sum = 0;
            std::int64_t value;
            while (true) {
                if (queue.try_pop(value)) {
                    sum += value;
                    idle.reset();
                } else if (done.load(std::memory_order_acquire)) {
                    if (!queue.try_pop(value)) {
                        break;
                    }
                    sum += value;
                } else {
                    idle.wait();
                }
            }
            sums[c] = sum;
        });
    }
    BackoffWhenIdle idle;
    for (std::int64_t i = 0; i < items; ++i) {
        while (!queue.try_push(i)) {
            idle.wait();
        }
        idle.reset();
    }
    done.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    const double ms = elapsed_ms(start);
    std::int64_t total = 0;
    for (const std::int64_t sum : sums) {
        total += sum;
    }
    return total == items * (items - 1) / 2 ? static_cast<double>(items) / ms / 1000.0 : -1.0;
}

int run_fanout(std::int64_t items, int runs)
{
    pin_current_thread(static_cast<std::size_t>(ThreadRole::Producer));
    const std::array<std::size_t, 3> consumer_counts = {1, 2, 4};
    for (const std::size_t consumers : consumer_counts) {
        for (int run = 0; run < runs; ++run) {
            const std::array<std::string, 4> impls = {"sharded_uncached", "sharded_spsc", "sharded_sequenced", "mpmc"};
            for (std::size_t k = 0; k < impls.size(); ++k) {
                const std::string& impl = impls[(run + k) % impls.size()];
                double mps = 0;
                if (impl == "sharded_uncached") {
                    mps = fanout_sharded<UncachedIndexBuffer<std::int64_t, kRingCapacity>>(items, consumers);
                } else if (impl == "sharded_spsc") {
                    mps = fanout_sharded<CircularBuffer<std::int64_t, kRingCapacity>>(items, consumers);
                } else if (impl == "sharded_sequenced") {
                    mps = fanout_sharded<SequencedBuffer<std::int64_t, kRingCapacity>>(items, consumers);
                } else {
                    mps = fanout_mpmc(items, consumers);
                }
                if (mps < 0) {
                    std::cerr << "queue_bench: fanout lost items\n";
                    return 1;
                }
                std::cout << "fanout," << impl << ',' << consumers << ',' << run << ',' << mps << '\n';
            }
        }
    }
    return 0;
}

std::expected<std::unique_ptr<ItchBook>, std::string> decode_ring_batched(const fs::path& path, std::size_t max_batch)
{
    const auto file = MappedFile::open(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto book = std::make_unique<ItchBook>();
    auto pipeline = make_pipeline<std::unique_ptr<itch::Message>, kRingCapacity, YieldWhenIdle>(
        [&book](std::unique_ptr<itch::Message>& message) { itch::handle_message(message, book.get()); }, max_batch);
    std::thread parse_thread([&] {
        itch::FrameReader frames(file->bytes());
        while (const auto frame = frames.next()) {
            if (auto message = itch::decode_message(*frame)) {
                pipeline.push(std::move(message));
            }
        }
    });
    parse_thread.join();
    pipeline.finish();
    return book;
}

int run_shards(const fs::path& path, int runs)
{
    const std::uint64_t frames = count_frames(path);
    if (frames == 0) {
        std::cerr << "queue_bench: cannot read " << path << '\n';
        return 1;
    }
    const std::array<std::string, 7> impls = {"single", "ring", "ring_batch64", "sharded1", "sharded2", "sharded4", "sharded8"};
    auto decode = [&](const std::string& impl) -> std::expected<std::unique_ptr<ItchBook>, std::string> {
        if (impl == "single") {
            return ItchSingleThreadDecoder::decode_file(path);
        }
        if (impl == "ring") {
            return ItchDecoder::decode_file(path);
        }
        if (impl == "ring_batch64") {
            return decode_ring_batched(path, 64);
        }
        return ItchShardedDecoder::decode_file(path, std::stoul(impl.substr(7)));
    };
    for (const std::string& impl : impls) {
        if (!decode(impl)) {
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
            std::cout << "shards," << impl << ',' << frames << ',' << run << ',' << ms << '\n';
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
            return ItchSingleThreadDecoder::decode_file(path);
        }
        return impl == "mutex" ? ItchMutexDecoder::decode_file(path) : ItchDecoder::decode_file(path);
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

std::int64_t direct_call(std::int64_t items)
{
    pin_current_thread(static_cast<std::size_t>(ThreadRole::Producer));
    std::int64_t sum = 0;
    const auto consume = [&sum](std::int64_t value) { sum += value; };
    for (std::int64_t i = 0; i < items; ++i) {
        std::int64_t value = i;
        asm volatile("" : "+r"(value));
        consume(value);
    }
    return sum;
}

const std::array<std::string, 8> kOneImpls = {"direct", "mutex", "ring_yield", "ring_spin", "ring", "uncached_index",
                                              "sequenced", "mpmc"};

int run_one(const std::string& impl, std::int64_t items)
{
    g_items = items;
    const auto start = Clock::now();
    std::int64_t sum = 0;
    if (impl == "direct") {
        sum = direct_call(items);
    } else if (impl == "mutex") {
        sum = transfer<MutexQueue>(items);
    } else if (impl == "ring_yield") {
        sum = transfer<RingQueue<YieldWhenIdle>>(items);
    } else if (impl == "ring_spin") {
        sum = transfer<RingQueue<SpinWhenIdle>>(items);
    } else if (std::find(kVariantImpls.begin(), kVariantImpls.end(), impl) != kVariantImpls.end()) {
        sum = with_variant<FlatRun>(impl);
    } else {
        std::cerr << "queue_bench: unknown impl " << impl << ", one of:";
        for (const std::string& name : kOneImpls) {
            std::cerr << ' ' << name;
        }
        std::cerr << '\n';
        return 1;
    }
    const double ms = elapsed_ms(start);
    if (sum != items * (items - 1) / 2) {
        std::cerr << "queue_bench: " << impl << " lost items\n";
        return 1;
    }
    std::cout << "one," << impl << ',' << items << ',' << ms << ',' << static_cast<double>(items) / ms / 1000.0 << '\n';
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
    if (mode == "variants" && argc > 2) {
        prefer_performance_cores();
        return run_variants(std::atoi(argv[2]));
    }
    if (mode == "batching" && argc > 2) {
        prefer_performance_cores();
        return run_batching(std::atoi(argv[2]));
    }
    if (mode == "one" && argc > 3) {
        prefer_performance_cores();
        return run_one(argv[2], std::atoll(argv[3]));
    }
    if (argc < 4) {
        std::cerr << "usage: queue_bench queue <items> <runs>\n"
                  << "       queue_bench latency <msgs_per_sec> <runs> [seconds]\n"
                  << "       queue_bench decode <file.bin> <runs>\n"
                  << "       queue_bench sweep <runs>\n"
                  << "       queue_bench batching <runs>\n"
                  << "       queue_bench variants <runs>\n"
                  << "       queue_bench fanout <items> <runs>\n"
                  << "       queue_bench shards <file.bin> <runs>\n"
                  << "       queue_bench one <impl> <items>\n"
                  << "LOB_CPUS=a,b pins the producer to a and the consumer to b (fanout: consumers on b,c,...)\n";
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
    if (mode == "fanout") {
        return run_fanout(std::atoll(argv[2]), runs);
    }
    if (mode == "shards") {
        return run_shards(argv[2], runs);
    }
    std::cerr << "queue_bench: unknown mode " << mode << '\n';
    return 1;
}
