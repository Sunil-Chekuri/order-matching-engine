#pragma once

#include <map>
#include <deque>
#include <unordered_map>
#include <functional>
#include <cstddef>

#include "core/order.h"
#include "core/book_snapshot.h"

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

    // Aggregated top-N view of both sides. A depth of 0 yields an empty
    // snapshot; a depth beyond the number of populated levels yields
    // every level that exists, without padding.
    BookSnapshot snapshot(
        std::size_t depth);
};