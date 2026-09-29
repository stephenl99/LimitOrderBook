#include "Databento/AddOrderMessage.h"

namespace databento {

bool AddOrderMessage::decode(const std::vector<uint8_t>& data)
{
    return decode_mbo_body(data, Action::Add);
}

}  // namespace databento
