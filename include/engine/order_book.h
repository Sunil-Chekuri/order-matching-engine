#pragma once

#include <map>
#include <deque>
#include <unordered_map>
#include <functional>

#include "core/order.h"

class OrderBook
{
private:
    std::map<
        double,
        std::deque<Order>,
        std::greater<double>>
        bids;

    std::map<
        double,
        std::deque<Order>>
        asks;

    std::unordered_map<
        int,
        Order *>
        order_registry;

public:
    void addOrder(
        const Order &order);

    bool hasMatch();

    bool hasBids();

    bool hasAsks();

    Order &bestBid();

    Order &bestAsk();

    void removeBestBid();

    void removeBestAsk();

    bool cancelOrder(
        int order_id);

    int availableToMatch(
        Side incoming_side,
        double limit_price,
        int participant_id = 0);

    bool getRemainingQuantity(
        int order_id,
        int &out_quantity);
};