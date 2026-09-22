#include "engine/matching_engine.h"
#include "utils/logger.h"

#include <algorithm>

void MatchingEngine::processOrder(
    const Order &order)
{
    switch (order.type)
    {
        case OrderType::MARKET:
            // No price limit: sweep whatever liquidity exists. Any
            // unfilled remainder is discarded, never rests in the book.
            matchAggressively(order, false);
            return;

        case OrderType::IOC:
            // Same sweep, but only against prices that cross the
            // order's own limit price. Unfilled remainder is discarded.
            matchAggressively(order, true);
            return;

        case OrderType::FOK:
        {
            // All-or-nothing: only execute if the full quantity can be
            // filled immediately: otherwise the order is killed with no
            // trades and no partial fills at all.
            int available =
                book.availableToMatch(
                    order.side,
                    order.price,
                    order.participant_id);

            if (available >= order.quantity)
                matchAggressively(order, true);

            return;
        }

        case OrderType::LIMIT:
        default:
            break;
    }

    book.addOrder(order);

    while (book.hasMatch())
    {
        Order &buy =
            book.bestBid();

        Order &sell =
            book.bestAsk();

        // Self-trade prevention: stop matching entirely rather than let
        // the same participant's buy and sell trade against each other.
        // This is a simple "stop at top of book" policy, not a full
        // skip-ahead implementation: it does not look past a blocking
        // self-order to find other resting liquidity behind it. See
        // docs/DAILY_LOG.md (Day 6) for why, and what a fuller
        // implementation would need.
        if (buy.participant_id != 0 && buy.participant_id == sell.participant_id)
            break;

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

void MatchingEngine::matchAggressively(
    Order incoming,
    bool respect_price)
{
    while (incoming.quantity > 0)
    {
        bool oppositeAvailable =
            (incoming.side == Side::BUY)
                ? book.hasAsks()
                : book.hasBids();

        if (!oppositeAvailable)
            break;

        Order &resting =
            (incoming.side == Side::BUY)
                ? book.bestAsk()
                : book.bestBid();

        if (incoming.participant_id != 0 && incoming.participant_id == resting.participant_id)
            break;

        if (respect_price)
        {
            bool crosses =
                (incoming.side == Side::BUY)
                    ? (resting.price <= incoming.price)
                    : (resting.price >= incoming.price);

            if (!crosses)
                break;
        }

        int qty =
            std::min(
                incoming.quantity,
                resting.quantity);

        trade_counter++;

        int buy_id =
            (incoming.side == Side::BUY)
                ? incoming.order_id
                : resting.order_id;

        int sell_id =
            (incoming.side == Side::BUY)
                ? resting.order_id
                : incoming.order_id;

        Trade trade(
            trade_counter,
            buy_id,
            sell_id,
            resting.price,
            qty);

        total_trades++;

        if (total_trades % 1000 == 0)
        {
            Logger::log(
                LogLevel::INFO,
                "Trades executed: " + std::to_string(total_trades));
        }

        incoming.quantity -= qty;
        resting.quantity -= qty;

        if (resting.quantity == 0)
        {
            if (incoming.side == Side::BUY)
                book.removeBestAsk();
            else
                book.removeBestBid();
        }
    }
}

bool MatchingEngine::getRemainingQuantity(
    int order_id,
    int &out_quantity)
{
    return book.getRemainingQuantity(order_id, out_quantity);
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