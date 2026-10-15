//
// Created by Stephen Linder on 8/31/26.
//

#include "Book.h"

template class BasicBook<SecurityBook>;
template class BasicBook<FlatSecurityBook>;
template class BasicBook<HybridFlatSecurityBook>;
template class BasicBook<SortedAggregateSecurityBook>;
template class BasicBook<SortedAggregateFlatIndexSecurityBook>;
template class BasicBook<SortedAggregateFlatIndexSecurityBook, 1>;
template class BasicBook<LadderSecurityBook>;
template class BasicBook<LadderSecurityBook, 1>;
template class BasicBook<LadderSecurityBook, 64>;
template class BasicBook<IdentityLadderSecurityBook, 64>;
