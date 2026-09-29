#include "Databento/TradeMessage.h"

namespace databento {

bool TradeMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Trade);
}

}  // namespace databento
