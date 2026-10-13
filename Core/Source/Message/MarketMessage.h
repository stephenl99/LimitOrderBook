#pragma once

#include <cstdint>
#include <vector>

class MarketMessage {
public:
    virtual ~MarketMessage() = default;

    virtual bool decode(const std::vector<uint8_t>& data) = 0;

    [[nodiscard]] char message_type() const { return message_type_; }
    [[nodiscard]] uint64_t timestamp_ns() const { return timestamp_ns_; }

protected:
    char message_type_{};
    uint64_t timestamp_ns_{};
};
