#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace itch {

void copy_ascii_field(std::span<const uint8_t> data, size_t offset,
                      std::array<char, 8>& dest);
void copy_ascii_field4(std::span<const uint8_t> data, size_t offset,
                       std::array<char, 4>& dest);

}  // namespace itch
