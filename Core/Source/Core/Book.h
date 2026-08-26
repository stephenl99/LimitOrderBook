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

struct BetterAsk {
    bool operator()(const unique_ptr<Order>& a,
                    const unique_ptr<Order>& b) const {
        return a->price() < b->price();
    }
};

struct BetterBid {
    bool operator()(const unique_ptr<Order>& a,
                    const unique_ptr<Order>& b) const {
        return a->price() > b->price();
    }
};

class Book {

public:
    priority_queue<unique_ptr<Order>, vector<unique_ptr<Order>>, BetterBid> bid;
    priority_queue<unique_ptr<Order>, vector<unique_ptr<Order>>, BetterAsk> ask;

    void accept_order(Order &order);

    void insert(Order &order);
};


