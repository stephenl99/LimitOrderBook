#include "Message/Itch/HandleMessage.h"

#include "Book/InnerBook/Order.h"
#include "Message/Itch/AddOrderMessage.h"
#include "Message/Itch/OrderCancelMessage.h"
#include "Message/Itch/OrderDeleteMessage.h"
#include "Message/Itch/OrderExecutedMessage.h"
#include "Message/Itch/OrderExecutedWithPriceMessage.h"
#include "Message/Itch/OrderReplaceMessage.h"
#include "Message/Itch/StockDirectoryMessage.h"

namespace itch {

void handle_message(const std::unique_ptr<Message>& message, ItchBook* book)
{
    if (message == nullptr) {
        return;
    }

    const uint16_t stock_locate = message->stock_locate();
    SecurityBook& security_book = book->book_for(stock_locate);

    switch (message->message_type()) {
    case 'R': {
        const auto& directory_msg = dynamic_cast<const StockDirectoryMessage&>(*message);
        book->itch_directory.add(stock_locate, directory_msg.stock());
        break;
    }
    case 'A':
    case 'F': {
        const auto& add = dynamic_cast<const AddOrderMessage&>(*message);
        security_book.insert(Order(add.timestamp_ns(),
                                   stock_locate,
                                   add.order_reference_number(),
                                   add.side(),
                                   add.price(),
                                   add.shares()));
        break;
    }
    case 'P':
    case 'Q':
        break;
    case 'D': {
        const auto& to_delete = dynamic_cast<const OrderDeleteMessage&>(*message);
        security_book.delete_order(to_delete.order_reference_number());
        break;
    }
    case 'E': {
        const auto& to_execute = dynamic_cast<const OrderExecutedMessage&>(*message);
        security_book.execute_order(to_execute.order_reference_number(), to_execute.executed_shares());
        break;
    }
    case 'C': {
        const auto& to_execute = dynamic_cast<const OrderExecutedWithPriceMessage&>(*message);
        security_book.execute_order(to_execute.order_reference_number(), to_execute.executed_shares());
        break;
    }
    case 'X': {
        const auto& to_cancel = dynamic_cast<const OrderCancelMessage&>(*message);
        security_book.cancel_order(to_cancel.order_reference_number(), to_cancel.cancelled_shares());
        break;
    }
    case 'U': {
        const auto& to_replace = dynamic_cast<const OrderReplaceMessage&>(*message);
        security_book.replace_order(to_replace.original_order_reference(),
                                  to_replace.new_order_reference(),
                                  to_replace.price(),
                                  to_replace.shares());
        break;
    }
    default:
        break;
    }
}

}  // namespace itch
