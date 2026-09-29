#include "Databento/DecodeMessage.h"

#include "Databento/AddOrderMessage.h"
#include "Databento/CancelMessage.h"
#include "Databento/ClearMessage.h"
#include "Databento/FillMessage.h"
#include "Databento/MboMessage.h"
#include "Databento/ModifyMessage.h"
#include "Databento/TradeMessage.h"

namespace databento {

std::unique_ptr<itch::Message> decode_message(const std::vector<uint8_t>& data)
{
    if (data.size() <= MboMessage::kAction) {
        return nullptr;
    }

    std::unique_ptr<itch::Message> message;
    switch (static_cast<Action>(data[MboMessage::kAction])) {
    case Action::Add:
        message = std::make_unique<AddOrderMessage>();
        break;
    case Action::Cancel:
        message = std::make_unique<CancelMessage>();
        break;
    case Action::Modify:
        message = std::make_unique<ModifyMessage>();
        break;
    case Action::Trade:
        message = std::make_unique<TradeMessage>();
        break;
    case Action::Fill:
        message = std::make_unique<FillMessage>();
        break;
    case Action::Clear:
        message = std::make_unique<ClearMessage>();
        break;
    default:
        return nullptr;
    }

    if (!message->decode(data)) {
        return nullptr;
    }
    return message;
}

}  // namespace databento
