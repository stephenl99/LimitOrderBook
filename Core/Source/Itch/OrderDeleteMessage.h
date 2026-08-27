#pragma once

#include <cstdint>
#include <vector>

#include "Itch/Message.h"

namespace itch {

class OrderDeleteMessage : public Message {
public:
    bool decode(const std::vector<uint8_t>& data) override;

    [[nodiscard]] uint64_t order_reference_number() const { return order_reference_number_; }

    static constexpr size_t kPayloadSize = 19;

private:
    uint64_t order_reference_number_{};
};

}  // namespace itch
