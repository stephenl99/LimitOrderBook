#include "Databento/FillMessage.h"

namespace databento {

bool FillMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Fill);
}

}  // namespace databento
