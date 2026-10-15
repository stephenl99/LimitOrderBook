#include "Message/Databento/HandleMessage.h"

#include "Book/Order.h"
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
        if constexpr (requires { security_book.add_order(uint64_t{}, ::Side::BID, Price{}, uint32_t{}); }) {
            security_book.add_order(mbo.order_id(), mbo.side() == Side::Bid ? ::Side::BID : ::Side::ASK,
                                    mbo.raw_price(), mbo.size());
        } else {
            security_book.insert(Order(mbo.timestamp_ns(),
                                       mbo.instrument_id(),
                                       mbo.order_id(),
                                       static_cast<char>(mbo.side()),
                                       mbo.raw_price(),
                                       mbo.size()));
        }
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

template void handle_message<Book>(const MboMessage&, Book*);
template void handle_message<FlatBook>(const MboMessage&, FlatBook*);
template void handle_message<HybridFlatBook>(const MboMessage&, HybridFlatBook*);
template void handle_message<AggregateBook>(const MboMessage&, AggregateBook*);
template void handle_message<AggregateFlatIndexBook>(const MboMessage&, AggregateFlatIndexBook*);
template void handle_message<CachedAggregateFlatIndexBook>(const MboMessage&, CachedAggregateFlatIndexBook*);
template void handle_message<LadderBook>(const MboMessage&, LadderBook*);
template void handle_message<CachedLadderBook>(const MboMessage&, CachedLadderBook*);
template void handle_message<DirectCachedLadderBook>(const MboMessage&, DirectCachedLadderBook*);
template void handle_message<IdentityLadderBook>(const MboMessage&, IdentityLadderBook*);

}  // namespace mbo
