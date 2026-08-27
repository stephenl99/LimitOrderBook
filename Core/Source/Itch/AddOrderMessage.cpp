#include "Itch/AddOrderMessage.h"

#include "Itch/ItchFields.h"

namespace itch {

bool AddOrderMessage::decode_add_body(const std::vector<uint8_t>& data,
                                      char expected_type, size_t min_size)
{
    if (!decode_header(data, min_size, expected_type)) {
        return false;
    }
    order_reference_number_ = read_u64(data, kOrderReference);
    side_ = static_cast<char>(data[kBuySell]);
    shares_ = read_u32(data, kShares);
    copy_ascii_field(data, kStock, stock_);
    price_ = read_u32(data, kPrice);
    return true;
}

bool AddOrderMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_add_body(data, 'A', kPayloadSize);
}

}  // namespace itch
