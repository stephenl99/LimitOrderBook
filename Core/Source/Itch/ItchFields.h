#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace itch {

void copy_ascii_field(const std::vector<uint8_t>& data, size_t offset,
                      std::array<char, 8>& dest);
void copy_ascii_field4(const std::vector<uint8_t>& data, size_t offset,
                       std::array<char, 4>& dest);

}  // namespace itch
