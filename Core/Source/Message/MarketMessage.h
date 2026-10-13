#pragma once

#include <cstdint>
#include <span>

class MarketMessage {
public:
    virtual ~MarketMessage() = default;

    virtual bool decode(std::span<const uint8_t> data) = 0;

    char message_type() const { return message_type_; }
    uint64_t timestamp_ns() const { return timestamp_ns_; }

protected:
    char message_type_{};
    uint64_t timestamp_ns_{};
};
