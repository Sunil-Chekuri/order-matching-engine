#pragma once

#include <mutex>

#include "engine/order_book.h"
#include "core/trade.h"

class MatchingEngine
{
private:
    OrderBook book;

    int trade_counter = 0;

    int total_trades = 0;

    // Serialises every public operation. The lock lives here rather than
    // inside OrderBook because a whole submit-match-remove sequence has
    // to be atomic, and the book hands out references that the matching
    // loop mutates across several calls — see the note in order_book.h.
    //
    // mutable so the const observers can lock: reading an int while
    // another thread writes it is a data race regardless of how benign
    // it looks on x86.
    mutable std::mutex engine_mutex;

    // Private helpers below require engine_mutex to already be held by
    // the caller, and must never acquire it themselves: public entry
    // points lock exactly once, and a second acquisition of a
    // non-recursive mutex on the same thread would deadlock.
    void processOrderLocked(
        const Order &order);

    void matchAggressively(
        Order incoming,
        bool respect_price);

public:
    void processOrder(
        const Order &order);

    bool cancelOrder(
        int order_id);

    bool getRemainingQuantity(
        int order_id,
        int &out_quantity);

    BookSnapshot snapshot(
        std::size_t depth);

    int getTotalTrades() const;

    int getTradeCounter() const;
};
