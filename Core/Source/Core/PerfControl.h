#pragma once

#include <cstdlib>
#include <string_view>

#include <unistd.h>

class PerfCounters {
public:
    PerfCounters() : control_(fd_from_env("LOB_PERF_CTL_FD")), ack_(fd_from_env("LOB_PERF_ACK_FD")) {}

    void enable() const { send("enable\n"); }
    void disable() const { send("disable\n"); }

private:
    static int fd_from_env(const char* name)
    {
        const char* value = std::getenv(name);
        return value == nullptr ? -1 : std::atoi(value);
    }

    void send(std::string_view command) const
    {
        if (control_ < 0) {
            return;
        }
        if (write(control_, command.data(), command.size()) < 0) {
            return;
        }
        if (ack_ >= 0) {
            char reply[8];
            if (read(ack_, reply, sizeof(reply)) < 0) {
                return;
            }
        }
    }

    int control_;
    int ack_;
};

inline const PerfCounters& perf_counters()
{
    static const PerfCounters counters;
    return counters;
}
