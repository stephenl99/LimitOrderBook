#include "Databento/TradeMessage.h"

namespace mbo {

bool TradeMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Trade);
}

}  // namespace mbo
