#include "Databento/FillMessage.h"

namespace mbo {

bool FillMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Fill);
}

}  // namespace mbo
