#include "Message/Itch/ItchFields.h"

#include <algorithm>

namespace itch {

void copy_ascii_field(std::span<const uint8_t> data, size_t offset,
                      std::array<char, 8>& dest)
{
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
                dest.size(), dest.begin());
}

void copy_ascii_field4(std::span<const uint8_t> data, size_t offset,
                       std::array<char, 4>& dest)
{
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
                dest.size(), dest.begin());
}

}  // namespace itch
