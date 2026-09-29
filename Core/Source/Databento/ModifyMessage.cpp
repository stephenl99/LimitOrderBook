#include "Databento/ModifyMessage.h"

namespace mbo {

bool ModifyMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Modify);
}

}  // namespace mbo
