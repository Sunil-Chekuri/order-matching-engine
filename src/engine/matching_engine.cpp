#include "engine/matching_engine.h"
#include "utils/logger.h"

#include <algorithm>

MatchingEngine::SymbolBook &MatchingEngine::getOrCreateShard(
    const std::string &symbol)
{
    {
        std::lock_guard<std::mutex>
            read_lock(books_mutex);

        auto it = books.find(symbol);

        if (it != books.end())
            return *it->second;
    }

    std::lock_guard<std::mutex>
        write_lock(books_mutex);

    // Another thread may have created this symbol between the lookup
    // above releasing the lock and this one acquiring it.
    auto &slot = books[symbol];

    if (!slot)
        slot = std::make_unique<SymbolBook>();

    return *slot;
}

MatchingEngine::SymbolBook *MatchingEngine::findShard(
    const std::string &symbol)
{
    std::lock_guard<std::mutex>
        read_lock(books_mutex);

    auto it = books.find(symbol);

    if (it == books.end())
        return nullptr;

    return it->second.get();
}

void MatchingEngine::processOrder(
    const Order &order)
{
    SymbolBook &shard = getOrCreateShard(order.symbol);

    std::lock_guard<std::mutex>
        lock(shard.mutex);

    processOrderLocked(shard, order);
}

void MatchingEngine::processOrderLocked(
    SymbolBook &shard,
    const Order &order)
{
    switch (order.type)
    {
        case OrderType::MARKET:
            // No price limit: sweep whatever liquidity exists. Any
            // unfilled remainder is discarded, never rests in the book.
            matchAggressively(shard, order, false);
            return;

        case OrderType::IOC:
            // Same sweep, but only against prices that cross the
            // order's own limit price. Unfilled remainder is discarded.
            matchAggressively(shard, order, true);
            return;

        case OrderType::FOK:
        {
            // All-or-nothing: only execute if the full quantity can be
            // filled immediately: otherwise the order is killed with no
            // trades and no partial fills at all.
            int available =
                shard.book.availableToMatch(
                    order.side,
                    order.price,
                    order.participant_id);

            if (available >= order.quantity)
                matchAggressively(shard, order, true);

            return;
        }

        case OrderType::LIMIT:
        default:
            break;
    }

    shard.book.addOrder(order);

    while (shard.book.hasMatch())
    {
        Order &buy =
            shard.book.bestBid();

        Order &sell =
            shard.book.bestAsk();

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

        shard.total_trades++;

        Trade trade(
            shard.total_trades,
            buy.order_id,
            sell.order_id,
            sell.price,
            qty);

        // Log periodically (avoid spam)

        if (shard.total_trades % 1000 == 0)
        {
            Logger::log(
                LogLevel::INFO,
                "Trades executed: " + std::to_string(shard.total_trades));
        }

        buy.quantity -= qty;
        sell.quantity -= qty;

        if (buy.quantity == 0)
        {
            shard.book.removeBestBid();
        }

        if (sell.quantity == 0)
        {
            shard.book.removeBestAsk();
        }
    }
}

void MatchingEngine::matchAggressively(
    SymbolBook &shard,
    Order incoming,
    bool respect_price)
{
    while (incoming.quantity > 0)
    {
        bool oppositeAvailable =
            (incoming.side == Side::BUY)
                ? shard.book.hasAsks()
                : shard.book.hasBids();

        if (!oppositeAvailable)
            break;

        Order &resting =
            (incoming.side == Side::BUY)
                ? shard.book.bestAsk()
                : shard.book.bestBid();

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

        shard.total_trades++;

        int buy_id =
            (incoming.side == Side::BUY)
                ? incoming.order_id
                : resting.order_id;

        int sell_id =
            (incoming.side == Side::BUY)
                ? resting.order_id
                : incoming.order_id;

        Trade trade(
            shard.total_trades,
            buy_id,
            sell_id,
            resting.price,
            qty);

        if (shard.total_trades % 1000 == 0)
        {
            Logger::log(
                LogLevel::INFO,
                "Trades executed: " + std::to_string(shard.total_trades));
        }

        incoming.quantity -= qty;
        resting.quantity -= qty;

        if (resting.quantity == 0)
        {
            if (incoming.side == Side::BUY)
                shard.book.removeBestAsk();
            else
                shard.book.removeBestBid();
        }
    }
}

bool MatchingEngine::getRemainingQuantity(
    int order_id,
    int &out_quantity,
    const std::string &symbol)
{
    SymbolBook *shard = findShard(symbol);

    if (shard == nullptr)
        return false;

    std::lock_guard<std::mutex>
        lock(shard->mutex);

    return shard->book.getRemainingQuantity(order_id, out_quantity);
}

BookSnapshot MatchingEngine::snapshot(
    std::size_t depth,
    const std::string &symbol)
{
    SymbolBook *shard = findShard(symbol);

    if (shard == nullptr)
        return BookSnapshot();

    std::lock_guard<std::mutex>
        lock(shard->mutex);

    return shard->book.snapshot(depth);
}

int MatchingEngine::getTotalTrades() const
{
    std::lock_guard<std::mutex>
        map_lock(books_mutex);

    int total = 0;

    for (const auto &entry : books)
    {
        std::lock_guard<std::mutex>
            shard_lock(entry.second->mutex);

        total += entry.second->total_trades;
    }

    return total;
}

std::size_t MatchingEngine::symbolCount() const
{
    std::lock_guard<std::mutex>
        map_lock(books_mutex);

    return books.size();
}

bool MatchingEngine::cancelOrder(
    int order_id,
    const std::string &symbol)
{
    SymbolBook *shard = findShard(symbol);

    bool success = false;

    if (shard != nullptr)
    {
        std::lock_guard<std::mutex>
            lock(shard->mutex);

        success = shard->book.cancelOrder(order_id);
    }

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
