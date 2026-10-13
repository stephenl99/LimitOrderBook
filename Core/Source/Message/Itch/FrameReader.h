#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace itch {

class FrameReader {
public:
    explicit FrameReader(std::span<const uint8_t> bytes) : remaining_(bytes) {}

    std::optional<std::span<const uint8_t>> next()
    {
        if (remaining_.size() < kLengthPrefix) {
            return std::nullopt;
        }
        const std::size_t length = (static_cast<std::size_t>(remaining_[0]) << 8) | remaining_[1];
        if (length == 0 || remaining_.size() < kLengthPrefix + length) {
            return std::nullopt;
        }
        const auto frame = remaining_.subspan(kLengthPrefix, length);
        remaining_ = remaining_.subspan(kLengthPrefix + length);
        return frame;
    }

private:
    static constexpr std::size_t kLengthPrefix = 2;

    std::span<const uint8_t> remaining_;
};

}  // namespace itch
