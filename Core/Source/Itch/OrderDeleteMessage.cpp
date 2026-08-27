#include "Itch/OrderDeleteMessage.h"

namespace itch {

bool OrderDeleteMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'D')) {
        return false;
    }
    order_reference_number_ = read_u64(data, Offsets::kOrderReference);
    return true;
}

}  // namespace itch
