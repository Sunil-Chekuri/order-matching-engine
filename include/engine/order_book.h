#pragma once

#include <map>
#include <deque>
#include <unordered_map>
#include <functional>
#include <cstddef>

#include "core/order.h"
#include "core/book_snapshot.h"

// NOT internally synchronised, deliberately. Callers must serialise
// access themselves — MatchingEngine does, holding one lock across each
// whole operation.
//
// A per-method mutex in here would be worse than useless: bestBid() and
// bestAsk() hand out references into the internal deques, and the
// matching loop reads, mutates and then removes through those references
// across several separate calls. Locking inside each method would
// release the lock while a caller still holds a live reference, leaving
// every useful sequence racy while looking safe.
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

    // Orders currently resting on either side. O(1): the registry
    // already tracks exactly the live orders.
    std::size_t restingOrderCount() const;

    // Aggregated top-N view of both sides. A depth of 0 yields an empty
    // snapshot; a depth beyond the number of populated levels yields
    // every level that exists, without padding.
    BookSnapshot snapshot(
        std::size_t depth);
};