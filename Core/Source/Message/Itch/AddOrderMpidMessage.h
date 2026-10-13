#pragma once

#include <array>

#include "Message/Itch/AddOrderMessage.h"

namespace itch {

class AddOrderMpidMessage : public AddOrderMessage {
public:
    bool decode(std::span<const uint8_t> data) override;

    const char* attribution() const { return attribution_.data(); }

    static constexpr size_t kPayloadSize = 40;
    static constexpr size_t kAttribution = 36;

private:
    std::array<char, 4> attribution_{};
};

}  // namespace itch
