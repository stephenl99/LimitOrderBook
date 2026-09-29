#include "Order.h"

#include <utility>


Order::Order(uint64_t timestamp_ns,
             InstrumentId stock_locate,
             uint64_t order_reference_number,
             char side,
             Price price,
             uint32_t quantity)
    : timestamp_ns_(timestamp_ns)
    , stock_locate_(stock_locate)
    , order_reference_number_(order_reference_number)
    , price_(price)
    , quantity_(quantity)
{
    if (side == 'B') {
        side_ = Side::BID;
    } else if (side == 'S' || side == 'A') {
        side_ = Side::ASK;
    }
}

Order::Order(Order&& other) noexcept {
    this->timestamp_ns_ = other.timestamp_ns();
    this->stock_locate_ = other.stock_locate();
    this->order_reference_number_ = other.order_reference_number();
    this->price_ = other.price_;
    this->quantity_ = other.quantity_;
    this->side_ = other.side();
}

Order::Order(const Order& other) {
    this->timestamp_ns_ = other.timestamp_ns();
    this->stock_locate_ = other.stock_locate();
    this->order_reference_number_ = other.order_reference_number();
    this->price_ = other.price_;
    this->quantity_ = other.quantity_;
    this->side_ = other.side();
}

bool Order::operator<(const Order& other) const
{
    if (side_ == Side::BID) {
        return price_ > other.price_;
    }
    return price_ < other.price_;
}
