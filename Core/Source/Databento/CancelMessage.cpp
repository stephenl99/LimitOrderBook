#include "Databento/CancelMessage.h"

namespace databento {

bool CancelMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Cancel);
}

}  // namespace databento
