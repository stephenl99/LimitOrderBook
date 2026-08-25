#include "Order.h"

Order::Order(uint64_t timestamp_ns,
             uint64_t order_reference_number,
             char side,
             uint32_t price,
             uint32_t quantity)
    : timestamp_ns_(timestamp_ns)
    , order_reference_number_(order_reference_number)
    , price_(price)
    , quantity_(quantity)
{
    if (side == 'B') {
        side_ = Side::BID;
    } else if (side == 'S') {
        side_ = Side::ASK;
    }
}

bool Order::operator<(const Order& other) const
{
    if (side_ == Side::BID) {
        return price_ > other.price_;  // max-heap → best bid on top
    }
    return price_ < other.price_;      // min-heap → best ask on top
}
