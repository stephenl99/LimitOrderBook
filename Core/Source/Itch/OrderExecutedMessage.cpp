#include "Itch/OrderExecutedMessage.h"

namespace itch {

bool OrderExecutedMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'E')) {
        return false;
    }
    order_reference_number_ = read_u64(data, Offsets::kOrderReference);
    executed_shares_ = read_u32(data, kExecutedShares);
    match_number_ = read_u64(data, kMatchNumber);
    return true;
}

}  // namespace itch
