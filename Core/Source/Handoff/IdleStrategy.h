#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef __linux__
#include <sys/prctl.h>
#endif

inline void cpu_pause()
{
#if defined(__aarch64__)
    __builtin_arm_isb(15);
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

template <typename T>
concept IdleStrategy = std::default_initializable<T> && requires(T idle) {
    idle.wait();
    idle.reset();
};

struct YieldWhenIdle {
    void wait() { std::this_thread::yield(); }
    void reset() {}
};

struct SpinWhenIdle {
    void wait() { cpu_pause(); }
    void reset() {}
};

template <std::uint32_t SpinAttempts, std::uint32_t YieldAttempts>
class BasicBackoff {
public:
    static constexpr std::uint32_t kSpinAttempts = SpinAttempts;
    static constexpr std::uint32_t kYieldAttempts = YieldAttempts;

    void wait()
    {
        if (attempts_ < kSpinAttempts) {
            cpu_pause();
        } else if (attempts_ < kSpinAttempts + kYieldAttempts) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(kSleep);
            return;
        }
        ++attempts_;
    }

    void reset()
    {
        if (attempts_ != 0) {
            attempts_ = 0;
        }
    }

private:
    static constexpr std::chrono::microseconds kSleep{20};

    std::uint32_t attempts_ = 0;
};

using LowLatencyBackoff = BasicBackoff<16000, 20000>;
using BalancedBackoff = BasicBackoff<256, 2000>;
using LowCpuBackoff = BasicBackoff<0, 64>;
using BackoffWhenIdle = BalancedBackoff;

struct BackoffSchedule {
    std::chrono::nanoseconds spin{500'000};
    std::chrono::nanoseconds yield{0};
    std::chrono::nanoseconds sleep{5'000};
    std::chrono::nanoseconds timer_slack{1'000};
};

inline BackoffSchedule backoff_schedule_from_env()
{
    BackoffSchedule schedule;
    const char* text = std::getenv("LOB_BACKOFF");
    if (text == nullptr) {
        return schedule;
    }
    std::chrono::nanoseconds* fields[] = {&schedule.spin, &schedule.yield, &schedule.sleep, &schedule.timer_slack};
    const std::string list = text;
    std::size_t start = 0;
    for (std::chrono::nanoseconds* field : fields) {
        if (start > list.size()) {
            break;
        }
        const std::size_t end = std::min(list.find(',', start), list.size());
        if (end > start) {
            *field = std::chrono::nanoseconds{std::stoll(list.substr(start, end - start))};
        }
        start = end + 1;
    }
    return schedule;
}

inline const BackoffSchedule& backoff_schedule()
{
    static const BackoffSchedule schedule = backoff_schedule_from_env();
    return schedule;
}

class TimedBackoff {
public:
    void wait()
    {
        const auto now = std::chrono::steady_clock::now();
        if (!waiting_) {
            waiting_ = true;
            started_ = now;
            apply_timer_slack();
        }
        const BackoffSchedule& schedule = backoff_schedule();
        const auto idle = now - started_;
        if (idle < schedule.spin) {
            cpu_pause();
        } else if (idle < schedule.spin + schedule.yield) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(schedule.sleep);
        }
    }

    void reset()
    {
        if (waiting_) {
            waiting_ = false;
        }
    }

private:
    static void apply_timer_slack()
    {
#ifdef __linux__
        thread_local bool applied = false;
        const auto slack = backoff_schedule().timer_slack;
        if (!applied && slack.count() > 0) {
            prctl(PR_SET_TIMERSLACK, static_cast<unsigned long>(slack.count()), 0, 0, 0);
            applied = true;
        }
#endif
    }

    bool waiting_ = false;
    std::chrono::steady_clock::time_point started_{};
};
