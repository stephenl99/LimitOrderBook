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

    Order(Order&& other) noexcept;

    [[nodiscard]] uint64_t timestamp_ns() const { return timestamp_ns_; }
    [[nodiscard]] uint64_t order_reference_number() const { return order_reference_number_; }
    [[nodiscard]] Side side() const { return side_; }
    [[nodiscard]] uint32_t price() const { return price_; }
    [[nodiscard]] uint32_t quantity() const { return quantity_; }

    bool operator<(const Order& other) const;

private:
    uint64_t timestamp_ns_;
    uint64_t order_reference_number_;
    Side side_;
    uint32_t price_;
    uint32_t quantity_;
};
