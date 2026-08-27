#include "Itch/TradeMessage.h"

#include "Itch/ItchFields.h"

namespace itch {

bool TradeMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'P')) {
        return false;
    }
    order_reference_number_ = read_u64(data, Offsets::kOrderReference);
    side_ = static_cast<char>(data[Offsets::kBuySell]);
    shares_ = read_u32(data, Offsets::kShares);
    copy_ascii_field(data, Offsets::kStock, stock_);
    price_ = read_u32(data, Offsets::kPrice);
    match_number_ = read_u64(data, kMatchNumber);
    return true;
}

}  // namespace itch
