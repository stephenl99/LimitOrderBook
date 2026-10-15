#pragma once

#include "Book/Book.h"
#include "Message/Databento/MboMessage.h"

namespace mbo {

template <typename BookType>
void handle_message(const MboMessage& mbo, BookType* book);

extern template void handle_message<Book>(const MboMessage&, Book*);
extern template void handle_message<FlatBook>(const MboMessage&, FlatBook*);
extern template void handle_message<HybridFlatBook>(const MboMessage&, HybridFlatBook*);
extern template void handle_message<AggregateBook>(const MboMessage&, AggregateBook*);
extern template void handle_message<AggregateFlatIndexBook>(const MboMessage&, AggregateFlatIndexBook*);
extern template void handle_message<CachedAggregateFlatIndexBook>(const MboMessage&, CachedAggregateFlatIndexBook*);
extern template void handle_message<LadderBook>(const MboMessage&, LadderBook*);
extern template void handle_message<CachedLadderBook>(const MboMessage&, CachedLadderBook*);
extern template void handle_message<DirectCachedLadderBook>(const MboMessage&, DirectCachedLadderBook*);
extern template void handle_message<IdentityLadderBook>(const MboMessage&, IdentityLadderBook*);

}  // namespace mbo
