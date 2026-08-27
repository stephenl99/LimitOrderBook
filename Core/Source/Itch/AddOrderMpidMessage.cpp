#include "Itch/AddOrderMpidMessage.h"

#include "Itch/ItchFields.h"

namespace itch {

bool AddOrderMpidMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_add_body(data, 'F', kPayloadSize)) {
        return false;
    }
    copy_ascii_field4(data, kAttribution, attribution_);
    return true;
}

}  // namespace itch
