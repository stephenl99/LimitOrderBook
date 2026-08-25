#pragma once

#include <cstdint>

enum class Side {
    BID,
    ASK,
};

class Order {
public:
    Order(uint64_t timestamp_ns,
          uint64_t order_reference_number,
          char side,
          uint32_t price,
          uint32_t quantity);

    [[nodiscard]] uint64_t timestamp_ns1() const { return timestamp_ns_; }
    [[nodiscard]] uint64_t order_reference_number1() const { return order_reference_number_; }
    [[nodiscard]] Side side1() const { return side_; }
    [[nodiscard]] uint32_t price1() const { return price_; }
    [[nodiscard]] uint32_t quantity1() const { return quantity_; }

    bool operator<(const Order& other) const;

private:
    uint64_t timestamp_ns_;
    uint64_t order_reference_number_;
    Side side_;
    uint32_t price_;
    uint32_t quantity_;
};
