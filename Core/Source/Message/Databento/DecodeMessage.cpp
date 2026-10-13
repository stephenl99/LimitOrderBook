#include "Message/Databento/DecodeMessage.h"

#include "Message/Databento/AddOrderMessage.h"
#include "Message/Databento/CancelMessage.h"
#include "Message/Databento/ClearMessage.h"
#include "Message/Databento/FillMessage.h"
#include "Message/Databento/MboMessage.h"
#include "Message/Databento/ModifyMessage.h"
#include "Message/Databento/TradeMessage.h"
//market by order parsing
namespace mbo {

std::unique_ptr<MboMessage> decode_message(const std::vector<uint8_t>& data)
{
    if (data.size() <= MboMessage::kAction) {
        return nullptr;
    }

    std::unique_ptr<MboMessage> message;
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

}  // namespace mbo
