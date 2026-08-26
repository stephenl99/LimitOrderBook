//
// Created by Stephen Linder on 8/22/26.
//

#include "Book.h"

#include <queue>
#include<vector>

#include "Order.h"

using namespace std;


void Book::accept_order(Order& order) {
    auto ptr = make_unique<Order>(std::move(order));
    if (ptr->side() == Side::ASK) {
        this->ask.emplace(std::move(ptr));
    } else {
        this->bid.emplace(std::move(ptr));
    }
}

void Book::insert(Order& order) {
    accept_order(order);
}