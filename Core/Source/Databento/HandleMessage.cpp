#include "Databento/HandleMessage.h"

#include "Core/Order.h"
#include "Databento/MboMessage.h"

namespace mbo {

void handle_message(const std::unique_ptr<itch::Message>& message, Book* book)
{
    if (message == nullptr || book == nullptr) {
        return;
    }

    const auto* mbo = dynamic_cast<const MboMessage*>(message.get());
    if (mbo == nullptr) {
        return;
    }

    switch (mbo->action()) {
    case Action::Add:
        book->book_for(mbo->instrument_id())
            .insert(std::make_unique<Order>(mbo->timestamp_ns(),
                                            mbo->instrument_id(),
                                            mbo->order_id(),
                                            static_cast<char>(mbo->side()),
                                            mbo->raw_price(),
                                            mbo->size()));
        break;
    case Action::Cancel:
        book->book_for(mbo->instrument_id()).cancel_order(mbo->order_id(), mbo->size());
        break;
    case Action::Modify:
        book->book_for(mbo->instrument_id()).modify_order(mbo->order_id(), mbo->raw_price(), mbo->size());
        break;
    case Action::Clear:
        book->book_for(mbo->instrument_id()).clear();
        break;
    case Action::Fill:
    case Action::Trade:
    case Action::None:
        break;
    }
}

}  // namespace mbo
