#include "Itch/DecodeMessage.h"

#include <memory>

#include "Itch/AddOrderMessage.h"
#include "Itch/AddOrderMpidMessage.h"
#include "Itch/CrossTradeMessage.h"
#include "Itch/OrderCancelMessage.h"
#include "Itch/OrderDeleteMessage.h"
#include "Itch/OrderExecutedMessage.h"
#include "Itch/OrderExecutedWithPriceMessage.h"
#include "Itch/OrderReplaceMessage.h"
#include "Itch/TradeMessage.h"

namespace itch {

std::unique_ptr<Message> decode_message(const std::vector<uint8_t>& data)
{
    if (data.empty()) {
        return nullptr;
    }

    std::unique_ptr<Message> message;
    switch (data[0]) {
    case 'A':
        message = std::make_unique<AddOrderMessage>();
        break;
    case 'F':
        message = std::make_unique<AddOrderMpidMessage>();
        break;
    case 'E':
        message = std::make_unique<OrderExecutedMessage>();
        break;
    case 'C':
        message = std::make_unique<OrderExecutedWithPriceMessage>();
        break;
    case 'X':
        message = std::make_unique<OrderCancelMessage>();
        break;
    case 'D':
        message = std::make_unique<OrderDeleteMessage>();
        break;
    case 'U':
        message = std::make_unique<OrderReplaceMessage>();
        break;
    case 'P':
        message = std::make_unique<TradeMessage>();
        break;
    case 'Q':
        message = std::make_unique<CrossTradeMessage>();
        break;
    default:
        return nullptr;
    }

    if (!message->decode(data)) {
        return nullptr;
    }
    return message;
}

}
