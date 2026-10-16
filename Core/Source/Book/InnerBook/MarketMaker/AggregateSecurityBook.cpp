#include "AggregateSecurityBook.h"

#include <iostream>

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::add_resting(uint64_t id, Side side, Price price, uint32_t quantity)
{
    on_side(side, [&](auto& levels) { levels.add(price, quantity); });
    orders_.insert(id, RestingOrder{price, ++next_sequence_, quantity, side});
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::remove_resting(typename Orders::Handle handle)
{
    const RestingOrder& order = orders_.get(handle);
    on_side(order.side, [&](auto& levels) { levels.remove(order.price, order.quantity); });
    orders_.erase(handle);
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::insert(const Order& order)
{
    add_resting(order.order_reference_number(), order.side(), order.price(), order.quantity());
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::delete_order(uint64_t order_reference_number)
{
    const auto handle = orders_.find(order_reference_number);
    if (!handle) {
        std::cout << "Attempted to delete order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }
    remove_resting(handle);
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::reduce_order_quantity(uint64_t id, uint32_t shares, const char* action)
{
    const auto handle = orders_.find(id);
    if (!handle) {
        std::cout << "Attempted to " << action << " order " << id << ", but it was no longer in the book" << std::endl;
        return;
    }

    RestingOrder& order = orders_.get(handle);
    if (shares > order.quantity) {
        std::cout << "Attempted to " << action << " " << shares << " shares of order " << id << ", but only "
                  << order.quantity << " remained" << std::endl;
        remove_resting(handle);
        return;
    }

    const uint32_t new_quantity = order.quantity - shares;
    if (new_quantity == 0) {
        remove_resting(handle);
        return;
    }

    on_side(order.side, [&](auto& levels) { levels.adjust(order.price, -static_cast<int64_t>(shares)); });
    order.quantity = new_quantity;
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::execute_order(uint64_t order_reference_number, uint32_t executed_shares)
{
    reduce_order_quantity(order_reference_number, executed_shares, "execute");
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::cancel_order(uint64_t order_reference_number, uint32_t cancelled_shares)
{
    reduce_order_quantity(order_reference_number, cancelled_shares, "cancel");
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::replace_order(uint64_t old_order_reference_number,
                                                          uint64_t new_order_reference_number,
                                                          Price new_price,
                                                          uint32_t new_shares)
{
    const auto handle = orders_.find(old_order_reference_number);
    if (!handle) {
        std::cout << "Attempted to replace order " << old_order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }
    const Side side = orders_.get(handle).side;
    remove_resting(handle);
    add_resting(new_order_reference_number, side, new_price, new_shares);
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::modify_order(uint64_t order_reference_number, Price new_price, uint32_t new_shares)
{
    const auto handle = orders_.find(order_reference_number);
    if (!handle) {
        std::cout << "Attempted to modify order " << order_reference_number
                  << ", but it was no longer in the book" << std::endl;
        return;
    }

    RestingOrder& order = orders_.get(handle);
    const int64_t quantity_change = static_cast<int64_t>(new_shares) - static_cast<int64_t>(order.quantity);
    const bool keeps_priority = new_price == order.price && new_shares <= order.quantity;
    if (keeps_priority && new_shares > 0) {
        on_side(order.side, [&](auto& levels) { levels.adjust(order.price, quantity_change); });
        order.quantity = new_shares;
        return;
    }
    if (new_shares == 0) {
        remove_resting(handle);
        return;
    }

    on_side(order.side, [&](auto& levels) {
        if (new_price == order.price) {
            levels.adjust(order.price, quantity_change);
        } else {
            levels.remove(order.price, order.quantity);
            levels.add(new_price, new_shares);
        }
    });
    order.price = new_price;
    order.quantity = new_shares;
    order.sequence = ++next_sequence_;
}

template <typename Levels, typename Orders>
void AggregateSecurityBook<Levels, Orders>::clear()
{
    orders_.clear();
    bid_levels_.clear();
    ask_levels_.clear();
}

template class AggregateSecurityBook<SortedAggregateLevels, NodeOrderIndex>;
template class AggregateSecurityBook<SortedAggregateLevels, FlatOrderIndex>;
template class AggregateSecurityBook<LadderLevels, FlatOrderIndex>;
template class AggregateSecurityBook<LadderLevels, IdentityFlatOrderIndex>;
template class AggregateSecurityBook<MapAggregateLevels, FlatOrderIndex>;
template class AggregateSecurityBook<LinearAggregateLevels, FlatOrderIndex>;
