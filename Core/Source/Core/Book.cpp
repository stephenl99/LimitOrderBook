//
// Created by Stephen Linder on 8/22/26.
//

#include "Book.h"

#include <queue>
#include<vector>

#include "Order.h"

using namespace std;


void Book::AcceptOrder(const Order& order) {
    if (order.side1() == Side::ASK) {
        this->ask.emplace(order);
    } else {
        this->bid.emplace(order);
    }
};
void Book::insert(Order* order) {
    if (order == nullptr) {
        return;
    }
    AcceptOrder(*order);
    delete order;
}