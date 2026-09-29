#include "Databento/ClearMessage.h"

namespace databento {

bool ClearMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Clear);
}

}  // namespace databento
