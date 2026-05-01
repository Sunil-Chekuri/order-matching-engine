#include "engine/matching_engine.h"
#include "utils/logger.h"

#include <algorithm>

void MatchingEngine::processOrder(
    const Order &order)
{
    book.addOrder(order);

    while (book.hasMatch())
    {
        Order &buy =
            book.bestBid();

        Order &sell =
            book.bestAsk();

        int qty =
            std::min(
                buy.quantity,
                sell.quantity);

        trade_counter++;

        Trade trade(
            trade_counter,
            buy.order_id,
            sell.order_id,
            sell.price,
            qty);

        total_trades++;

        // Log periodically (avoid spam)

        if (total_trades % 1000 == 0)
        {
            Logger::log(
                LogLevel::INFO,
                "Trades executed: " + std::to_string(total_trades));
        }

        buy.quantity -= qty;
        sell.quantity -= qty;

        if (buy.quantity == 0)
        {
            book.removeBestBid();
        }

        if (sell.quantity == 0)
        {
            book.removeBestAsk();
        }
    }
}

bool MatchingEngine::cancelOrder(
    int order_id)
{
    bool success =
        book.cancelOrder(order_id);

    if (success)
    {
        Logger::log(
            LogLevel::INFO,
            "Order cancelled: " + std::to_string(order_id));
    }
    else
    {
        Logger::log(
            LogLevel::WARNING,
            "Cancel failed for order: " + std::to_string(order_id));
    }

    return success;
}