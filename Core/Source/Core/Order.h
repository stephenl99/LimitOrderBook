#pragma once

#include <cstdint>

enum class Side {
    BID,
    ASK,
};

class Order {
public:
    void set_timestamp_ns(uint64_t timestamp_ns) {
        timestamp_ns_ = timestamp_ns;
    }

    void set_stock_locate(uint16_t stock_locate) {
        stock_locate_ = stock_locate;
    }

    void set_order_reference_number(uint64_t order_reference_number) {
        order_reference_number_ = order_reference_number;
    }

    void set_side(Side side) {
        side_ = side;
    }

    void set_price(uint32_t price) {
        price_ = price;
    }

    void set_quantity(uint32_t quantity) {
        quantity_ = quantity;
    }

    Order(uint64_t timestamp_ns,
          uint16_t stock_locate,
          uint64_t order_reference_number,
          char side,
          uint32_t price,
          uint32_t quantity);

    Order(Order&& other) noexcept;

    Order(const Order& other);

    [[nodiscard]] uint64_t timestamp_ns() const { return timestamp_ns_; }
    [[nodiscard]] uint16_t stock_locate() const { return stock_locate_; }
    [[nodiscard]] uint64_t order_reference_number() const { return order_reference_number_; }
    [[nodiscard]] Side side() const { return side_; }
    [[nodiscard]] uint32_t price() const { return price_; }
    [[nodiscard]] uint32_t quantity() const { return quantity_; }

    bool operator<(const Order& other) const;

private:
    uint64_t timestamp_ns_;
    uint16_t stock_locate_;
    uint64_t order_reference_number_;
    Side side_;
    uint32_t price_;
    uint32_t quantity_;
};
