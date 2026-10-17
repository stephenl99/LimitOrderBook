#pragma once

#include <concepts>
#include <cstddef>

inline constexpr std::size_t kCacheLineSize = 128;

template <typename T>
concept Usable = std::movable<T> && std::default_initializable<T>;
