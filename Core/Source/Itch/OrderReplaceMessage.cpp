#include "Itch/OrderReplaceMessage.h"

namespace itch {

bool OrderReplaceMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'U')) {
        return false;
    }
    original_order_reference_ = read_u64(data, Offsets::kOrderReference);
    new_order_reference_ = read_u64(data, kNewOrderReference);
    shares_ = read_u32(data, kShares);
    price_ = read_u32(data, kPrice);
    return true;
}

}  // namespace itch
