#pragma once

#include "engine/order_book.h"
#include "core/trade.h"

class MatchingEngine
{
private:
    OrderBook book;

    int trade_counter = 0;

    int total_trades = 0;

public:
    void processOrder(
        const Order &order);

    bool cancelOrder(
        int order_id);

    int getTotalTrades() const
    {
        return total_trades;
    }

    int getTradeCounter() const
    {
        return trade_counter;
    }
};