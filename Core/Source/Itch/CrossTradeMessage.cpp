#include "Itch/CrossTradeMessage.h"

#include "Itch/ItchFields.h"

namespace itch {

bool CrossTradeMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'Q')) {
        return false;
    }
    shares_ = read_u32(data, kShares);
    copy_ascii_field(data, kStock, stock_);
    cross_price_ = read_u32(data, kCrossPrice);
    match_number_ = read_u64(data, kMatchNumber);
    cross_type_ = static_cast<char>(data[kCrossType]);
    return true;
}

}  // namespace itch
