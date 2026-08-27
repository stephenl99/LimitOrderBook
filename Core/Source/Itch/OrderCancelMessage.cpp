#include "Itch/OrderCancelMessage.h"

namespace itch {

bool OrderCancelMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'X')) {
        return false;
    }
    order_reference_number_ = read_u64(data, Offsets::kOrderReference);
    cancelled_shares_ = read_u32(data, kCancelledShares);
    return true;
}

}  // namespace itch
