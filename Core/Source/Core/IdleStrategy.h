#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <thread>

enum class PauseKind {
    Yield,
    Isb,
};

template <PauseKind Kind = PauseKind::Yield>
inline void cpu_pause()
{
#if defined(__aarch64__)
    if constexpr (Kind == PauseKind::Isb) {
        __builtin_arm_isb(15);
    } else {
        __builtin_arm_yield();
    }
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

template <PauseKind Kind = PauseKind::Yield>
struct BasicSpin {
    void wait() { cpu_pause<Kind>(); }
    void reset() {}
};

template <std::uint32_t SpinAttempts, std::uint32_t YieldAttempts, PauseKind Kind = PauseKind::Yield>
class BasicBackoff {
public:
    static constexpr std::uint32_t kSpinAttempts = SpinAttempts;
    static constexpr std::uint32_t kYieldAttempts = YieldAttempts;

    void wait()
    {
        if (attempts_ < kSpinAttempts) {
            cpu_pause<Kind>();
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

using SpinWhenIdle = BasicSpin<>;
using LowLatencyBackoff = BasicBackoff<16000, 20000>;
using BalancedBackoff = BasicBackoff<256, 2000, PauseKind::Isb>;
using LowCpuBackoff = BasicBackoff<0, 64>;
using BackoffWhenIdle = BalancedBackoff;
