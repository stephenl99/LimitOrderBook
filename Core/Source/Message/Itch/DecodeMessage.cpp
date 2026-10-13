#include "Message/Itch/DecodeMessage.h"

#include <memory>

#include "Message/Itch/AddOrderMessage.h"
#include "Message/Itch/AddOrderMpidMessage.h"
#include "Message/Itch/CrossTradeMessage.h"
#include "Message/Itch/OrderCancelMessage.h"
#include "Message/Itch/OrderDeleteMessage.h"
#include "Message/Itch/OrderExecutedMessage.h"
#include "Message/Itch/OrderExecutedWithPriceMessage.h"
#include "Message/Itch/OrderReplaceMessage.h"
#include "Message/Itch/StockDirectoryMessage.h"
#include "Message/Itch/TradeMessage.h"

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
    case 'R':
        message = std::make_unique<StockDirectoryMessage>();
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
