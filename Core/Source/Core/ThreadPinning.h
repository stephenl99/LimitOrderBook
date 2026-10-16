#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

inline std::vector<int> pinned_cpus()
{
    std::vector<int> cpus;
    const char* list = std::getenv("LOB_CPUS");
    if (list == nullptr) {
        return cpus;
    }
    const std::string text = list;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = std::min(text.find(',', start), text.size());
        if (end > start) {
            cpus.push_back(std::stoi(text.substr(start, end - start)));
        }
        start = end + 1;
    }
    return cpus;
}

inline void pin_current_thread(std::size_t slot)
{
#ifdef __linux__
    static const std::vector<int> cpus = pinned_cpus();
    if (cpus.empty()) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpus[slot % cpus.size()], &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)slot;
#endif
}
