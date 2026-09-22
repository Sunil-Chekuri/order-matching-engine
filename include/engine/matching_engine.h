#pragma once

#include "engine/order_book.h"
#include "core/trade.h"

class MatchingEngine
{
private:
    OrderBook book;

    int trade_counter = 0;

    int total_trades = 0;

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

    int getTotalTrades() const
    {
        return total_trades;
    }

    int getTradeCounter() const
    {
        return trade_counter;
    }
};