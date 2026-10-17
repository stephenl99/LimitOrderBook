//
// Created by Stephen Linder on 8/31/26.
//

#include "Core/Logger.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {

std::mutex g_mutex;
Logger::Level g_min_level = Logger::Level::Info;
std::ostream* g_output = &std::cerr;

const char* level_name(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Error:
        return "ERROR";
    case Logger::Level::Warn:
        return "WARN";
    case Logger::Level::Info:
        return "INFO";
    case Logger::Level::Debug:
        return "DEBUG";
    }
    return "?";
}

void write(Logger::Level level, const std::string& message)
{
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(level) > static_cast<int>(g_min_level)) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    (*g_output) << std::put_time(&local_tm, "%F %T") << '.' << std::setw(3) << std::setfill('0') << ms.count()
                << ' ' << level_name(level) << ' ' << message << '\n';
}

}  // namespace

namespace Logger {

void set_min_level(Level level)
{
    {
        std::lock_guard lock(g_mutex);
        g_min_level = level;
    }
    debug("min level updated");
}

Level min_level()
{
    std::lock_guard lock(g_mutex);
    return g_min_level;
}

void set_output_stream(std::ostream& stream)
{
    std::lock_guard lock(g_mutex);
    g_output = &stream;
}

void error(const std::string& message)
{
    write(Level::Error, message);
}

void warn(const std::string& message)
{
    write(Level::Warn, message);
}

void info(const std::string& message)
{
    write(Level::Info, message);
}

void debug(const std::string& message)
{
    write(Level::Debug, message);
}

}  // namespace Logger
