#include "Message/Itch/AddOrderMpidMessage.h"

#include "Message/Itch/ItchFields.h"

namespace itch {

bool AddOrderMpidMessage::decode(std::span<const uint8_t> data)
{
    if (!decode_add_body(data, 'F', kPayloadSize)) {
        return false;
    }
    copy_ascii_field4(data, kAttribution, attribution_);
    return true;
}

}  // namespace itch
