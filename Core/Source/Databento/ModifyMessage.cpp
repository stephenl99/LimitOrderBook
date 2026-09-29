#include "Databento/ModifyMessage.h"

namespace databento {

bool ModifyMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Modify);
}

}  // namespace databento
