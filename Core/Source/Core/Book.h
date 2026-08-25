//
// Created by Stephen Linder on 8/22/26.
//
#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include<vector>
#include "Order.h"
using namespace std;

class Book {

public:
    priority_queue<Order> bid;
    priority_queue<Order> ask;

    void AcceptOrder(const Order &order);

    void insert(Order *order);
};


