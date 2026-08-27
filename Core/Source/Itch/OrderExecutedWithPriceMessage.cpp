#include "Itch/OrderExecutedWithPriceMessage.h"

namespace itch {

bool OrderExecutedWithPriceMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'C')) {
        return false;
    }
    order_reference_number_ = read_u64(data, Offsets::kOrderReference);
    executed_shares_ = read_u32(data, kExecutedShares);
    match_number_ = read_u64(data, kMatchNumber);
    printable_ = static_cast<char>(data[kPrintable]);
    execution_price_ = read_u32(data, kExecutionPrice);
    return true;
}

}  // namespace itch
