#include "Databento/CancelMessage.h"

namespace mbo {

bool CancelMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Cancel);
}

}  // namespace mbo
