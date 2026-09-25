#include "engine/order_book.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    // bids and asks are maps with different comparators, so this is a
    // template rather than two near-identical loops.
    template <typename LevelMap>
    void collectLevels(
        const LevelMap &levels,
        std::size_t depth,
        std::vector<PriceLevel> &out)
    {
        out.reserve(std::min(depth, levels.size()));

        for (const auto &level : levels)
        {
            if (out.size() >= depth)
                break;

            PriceLevel aggregated;
            aggregated.price = level.first;
            aggregated.total_quantity = 0;
            aggregated.order_count = 0;

            for (const auto &order : level.second)
            {
                aggregated.total_quantity += order.quantity;
                ++aggregated.order_count;
            }

            out.push_back(aggregated);
        }
    }
}

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

bool OrderBook::hasBids()
{
    return !bids.empty();
}

bool OrderBook::hasAsks()
{
    return !asks.empty();
}

Order &OrderBook::bestBid()
{
    if (bids.empty())
        throw std::out_of_range("No bids in the order book");

    return bids.begin()->second.front();
}

Order &OrderBook::bestAsk()
{
    if (asks.empty())
        throw std::out_of_range("No asks in the order book");

    return asks.begin()->second.front();
}

void OrderBook::removeBestBid()
{
    if (bids.empty())
        throw std::out_of_range("No bids in the order book");

    int filled_order_id = bids.begin()->second.front().order_id;

    bids.begin()->second.pop_front();

    if (bids.begin()->second.empty())
        bids.erase(bids.begin());

    order_registry.erase(filled_order_id);
}

void OrderBook::removeBestAsk()
{
    if (asks.empty())
        throw std::out_of_range("No asks in the order book");

    int filled_order_id = asks.begin()->second.front().order_id;

    asks.begin()->second.pop_front();

    if (asks.begin()->second.empty())
        asks.erase(asks.begin());

    order_registry.erase(filled_order_id);
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

int OrderBook::availableToMatch(
    Side incoming_side,
    double limit_price,
    int participant_id)
{
    int total = 0;

    if (incoming_side == Side::BUY)
    {
        for (const auto &level : asks)
        {
            if (level.first > limit_price)
                break;

            for (const auto &order : level.second)
            {
                // Matching stops the instant a self-trade would occur, so
                // liquidity behind a self-order is not actually reachable:
                // it must not be counted as available either, or an FOK
                // order could be told "yes, fully fillable" and then only
                // partially fill (or not fill at all) once matching
                // actually runs and hits that same self-order first.
                if (participant_id != 0 && order.participant_id == participant_id)
                    return total;

                total += order.quantity;
            }
        }
    }
    else
    {
        for (const auto &level : bids)
        {
            if (level.first < limit_price)
                break;

            for (const auto &order : level.second)
            {
                if (participant_id != 0 && order.participant_id == participant_id)
                    return total;

                total += order.quantity;
            }
        }
    }

    return total;
}

bool OrderBook::getRemainingQuantity(
    int order_id,
    int &out_quantity)
{
    auto it = order_registry.find(order_id);

    if (it == order_registry.end())
        return false;

    out_quantity = it->second->quantity;

    return true;
}

std::size_t OrderBook::restingOrderCount() const
{
    return order_registry.size();
}

BookSnapshot OrderBook::snapshot(
    std::size_t depth)
{
    BookSnapshot result;

    collectLevels(bids, depth, result.bids);
    collectLevels(asks, depth, result.asks);

    return result;
}
