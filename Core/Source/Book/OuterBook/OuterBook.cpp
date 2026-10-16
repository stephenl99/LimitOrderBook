//
// Created by Stephen Linder on 8/31/26.
//

#include "OuterBook.h"

template class OuterBook<SecurityBook>;
template class OuterBook<FlatSecurityBook>;
template class OuterBook<HybridFlatSecurityBook>;
template class OuterBook<SortedAggregateSecurityBook>;
template class OuterBook<SortedAggregateFlatIndexSecurityBook>;
template class OuterBook<LadderSecurityBook>;
template class OuterBook<LadderSecurityBook, 64>;
template class OuterBook<IdentityLadderSecurityBook, 64>;
