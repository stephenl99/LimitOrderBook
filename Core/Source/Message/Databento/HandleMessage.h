#pragma once

#include "Book/InnerBook/Order.h"
#include "Message/Databento/MboMessage.h"

namespace mbo {

template <typename BookType>
void handle_message(const MboMessage& mbo, BookType* book)
{
    if (book == nullptr) {
        return;
    }

    switch (mbo.action()) {
    case Action::Add: {
        auto& security_book = book->book_for(mbo.instrument_id());
        security_book.add_order(mbo.order_id(), mbo.side() == Side::Bid ? ::Side::BID : ::Side::ASK, mbo.raw_price(),
                                mbo.size());
        break;
    }
    case Action::Cancel:
        book->book_for(mbo.instrument_id()).cancel_order(mbo.order_id(), mbo.size());
        break;
    case Action::Modify:
        book->book_for(mbo.instrument_id()).modify_order(mbo.order_id(), mbo.raw_price(), mbo.size());
        break;
    case Action::Clear:
        book->book_for(mbo.instrument_id()).clear();
        break;
    case Action::Fill:
    case Action::Trade:
    case Action::None:
        break;
    }
}

}  // namespace mbo
