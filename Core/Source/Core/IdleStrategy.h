#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <thread>

inline void cpu_pause()
{
#if defined(__aarch64__)
    __builtin_arm_yield();
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

class BackoffWhenIdle {
public:
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

    void reset() { attempts_ = 0; }

private:
    static constexpr std::uint32_t kSpinAttempts = 2000;
    static constexpr std::uint32_t kYieldAttempts = 2000;
    static constexpr std::chrono::microseconds kSleep{20};

    std::uint32_t attempts_ = 0;
};
