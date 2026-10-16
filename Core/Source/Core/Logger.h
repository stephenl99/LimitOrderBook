//
// Created by Stephen Linder on 8/31/26.
//
#pragma once

#include <ostream>
#include <string>

namespace Logger {

enum class Level {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
};

void set_min_level(Level level);
Level min_level();

void set_output_stream(std::ostream& stream);

void error(const std::string& message);
void warn(const std::string& message);
void info(const std::string& message);
void debug(const std::string& message);

}  // namespace Logger
