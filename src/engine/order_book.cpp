#include "engine/order_book.h"

void OrderBook::addOrder(const Order &order)
{
    if (order.side == Side::BUY)
        bids[order.price]
            .push_back(order);
    else
        asks[order.price]
            .push_back(order);

    Order *ptr;

    if (order.side == Side::BUY)
        ptr = &bids[order.price]
                   .back();
    else
        ptr = &asks[order.price]
                   .back();

    order_registry[order.order_id] = ptr;
}

bool OrderBook::hasMatch()
{
    if (bids.empty() || asks.empty())
        return false;

    return bids.begin()->first >= asks.begin()->first;
}

Order &OrderBook::bestBid()
{
    return bids.begin()->second.front();
}

Order &OrderBook::bestAsk()
{
    return asks.begin()->second.front();
}

void OrderBook::removeBestBid()
{
    bids.begin()->second.pop_front();

    if (bids.begin()->second.empty())
        bids.erase(bids.begin());
}

void OrderBook::removeBestAsk()
{
    asks.begin()->second.pop_front();

    if (asks.begin()->second.empty())
        asks.erase(asks.begin());
}

bool OrderBook::cancelOrder(
    int order_id)
{
    auto it =
        order_registry.find(order_id);

    if (it == order_registry.end())
        return false;

    Order *order =
        it->second;

    if (order->side == Side::BUY)
    {
        auto price_it =
            bids.find(order->price);

        if (price_it == bids.end())
            return false;

        auto &queue =
            price_it->second;

        for (auto q_it =
                 queue.begin();
             q_it != queue.end();
             ++q_it)
        {
            if (q_it->order_id == order_id)
            {
                queue.erase(q_it);
                break;
            }
        }

        if (queue.empty())
            bids.erase(price_it);
    }
    else
    {
        auto price_it =
            asks.find(order->price);

        if (price_it == asks.end())
            return false;

        auto &queue =
            price_it->second;

        for (auto q_it =
                 queue.begin();
             q_it != queue.end();
             ++q_it)
        {
            if (q_it->order_id == order_id)
            {
                queue.erase(q_it);
                break;
            }
        }

        if (queue.empty())
            asks.erase(price_it);
    }

    order_registry.erase(it);

    return true;
}
