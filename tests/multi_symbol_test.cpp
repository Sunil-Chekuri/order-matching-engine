#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "core/order.h"
#include "engine/matching_engine.h"

TEST(MultiSymbolTest, OrderDefaultsToTheDefaultSymbol)
{
    Order order(1, 100.0, 10, Side::BUY);

    EXPECT_EQ(order.symbol, DEFAULT_SYMBOL);
}

TEST(MultiSymbolTest, OrderPreservesAnExplicitSymbol)
{
    Order order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL");

    EXPECT_EQ(order.symbol, "AAPL");
}

TEST(MultiSymbolTest, NoSymbolsExistUntilAnOrderArrives)
{
    MatchingEngine engine;

    EXPECT_EQ(engine.symbolCount(), 0u);

    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));

    EXPECT_EQ(engine.symbolCount(), 1u);
}

TEST(MultiSymbolTest, EachSymbolGetsItsOwnBook)
{
    MatchingEngine engine;

    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(2, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "MSFT"));
    engine.processOrder(Order(3, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "TSLA"));

    EXPECT_EQ(engine.symbolCount(), 3u);

    BookSnapshot apple = engine.snapshot(5, "AAPL");
    ASSERT_EQ(apple.bids.size(), 1u);
    EXPECT_EQ(apple.bids[0].order_count, 1);
}

TEST(MultiSymbolTest, OrdersOnDifferentSymbolsNeverMatchEachOther)
{
    // A buy and a sell that would cross on price, but on different
    // instruments. They must both rest, untouched.
    MatchingEngine engine;

    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 0, "MSFT"));

    EXPECT_EQ(engine.getTotalTrades(), 0);

    BookSnapshot apple = engine.snapshot(5, "AAPL");
    BookSnapshot msft = engine.snapshot(5, "MSFT");

    ASSERT_EQ(apple.bids.size(), 1u);
    EXPECT_TRUE(apple.asks.empty());

    ASSERT_EQ(msft.asks.size(), 1u);
    EXPECT_TRUE(msft.bids.empty());
}

TEST(MultiSymbolTest, OrdersOnTheSameSymbolStillMatchNormally)
{
    MatchingEngine engine;

    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 0, "AAPL"));

    EXPECT_EQ(engine.getTotalTrades(), 1);

    BookSnapshot apple = engine.snapshot(5, "AAPL");
    EXPECT_TRUE(apple.bids.empty());
    EXPECT_TRUE(apple.asks.empty());
}

TEST(MultiSymbolTest, TotalTradesSumsAcrossSymbols)
{
    MatchingEngine engine;

    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 0, "AAPL"));

    engine.processOrder(Order(3, 50.0, 5, Side::BUY, OrderType::LIMIT, 0, "MSFT"));
    engine.processOrder(Order(4, 50.0, 5, Side::SELL, OrderType::LIMIT, 0, "MSFT"));

    EXPECT_EQ(engine.getTotalTrades(), 2);
}

TEST(MultiSymbolTest, CancelRequiresTheCorrectSymbol)
{
    MatchingEngine engine;

    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));

    // Right id, wrong instrument: the order lives in another shard and
    // must not be found.
    EXPECT_FALSE(engine.cancelOrder(1, "MSFT"));

    EXPECT_TRUE(engine.cancelOrder(1, "AAPL"));
}

TEST(MultiSymbolTest, SameOrderIdCanExistOnDifferentSymbols)
{
    // Order ids are only unique within a symbol now, since each shard
    // keeps its own registry.
    MatchingEngine engine;

    engine.processOrder(Order(7, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(7, 200.0, 20, Side::BUY, OrderType::LIMIT, 0, "MSFT"));

    int quantity = -1;

    ASSERT_TRUE(engine.getRemainingQuantity(7, quantity, "AAPL"));
    EXPECT_EQ(quantity, 10);

    ASSERT_TRUE(engine.getRemainingQuantity(7, quantity, "MSFT"));
    EXPECT_EQ(quantity, 20);
}

TEST(MultiSymbolTest, QueriesForAnUnknownSymbolAreEmptyRatherThanFailures)
{
    MatchingEngine engine;

    BookSnapshot snapshot = engine.snapshot(5, "NOSUCH");
    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());

    int quantity = -1;
    EXPECT_FALSE(engine.getRemainingQuantity(1, quantity, "NOSUCH"));
    EXPECT_FALSE(engine.cancelOrder(1, "NOSUCH"));

    // Querying must not bring the symbol into existence.
    EXPECT_EQ(engine.symbolCount(), 0u);
}

TEST(MultiSymbolTest, ConcurrentTradingAcrossSymbolsStaysConsistent)
{
    // Four threads, each confined to its own instrument. Every order is
    // quantity 10 at one price, so each symbol must end with an empty
    // book and exactly one trade per submitted pair — independent of how
    // the threads interleave.
    MatchingEngine engine;

    const int thread_count = 4;
    const int pairs_per_thread = 500;

    std::vector<std::thread> workers;

    for (int t = 0; t < thread_count; ++t)
    {
        workers.emplace_back(
            [&engine, t]()
            {
                const std::string symbol = "SYM" + std::to_string(t);

                for (int i = 0; i < pairs_per_thread; ++i)
                {
                    engine.processOrder(
                        Order(i * 2 + 1, 100.0, 10, Side::BUY,
                              OrderType::LIMIT, 0, symbol));

                    engine.processOrder(
                        Order(i * 2 + 2, 100.0, 10, Side::SELL,
                              OrderType::LIMIT, 0, symbol));
                }
            });
    }

    for (auto &worker : workers)
        worker.join();

    EXPECT_EQ(engine.symbolCount(), static_cast<std::size_t>(thread_count));
    EXPECT_EQ(engine.getTotalTrades(), thread_count * pairs_per_thread);

    for (int t = 0; t < thread_count; ++t)
    {
        BookSnapshot snapshot = engine.snapshot(5, "SYM" + std::to_string(t));

        EXPECT_TRUE(snapshot.bids.empty());
        EXPECT_TRUE(snapshot.asks.empty());
    }
}

TEST(MultiSymbolTest, ConcurrentFirstTouchOfTheSameSymbolCreatesOneShard)
{
    // Several threads racing to be the first to trade a brand new
    // symbol must end up sharing one book, not creating several.
    MatchingEngine engine;

    const int thread_count = 4;

    std::vector<std::thread> workers;

    for (int t = 0; t < thread_count; ++t)
    {
        workers.emplace_back(
            [&engine, t]()
            {
                for (int i = 0; i < 100; ++i)
                {
                    engine.processOrder(
                        Order(t * 1000 + i, 100.0, 10, Side::BUY,
                              OrderType::LIMIT, 0, "SHARED"));
                }
            });
    }

    for (auto &worker : workers)
        worker.join();

    EXPECT_EQ(engine.symbolCount(), 1u);

    BookSnapshot snapshot = engine.snapshot(5, "SHARED");
    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_EQ(snapshot.bids[0].order_count, thread_count * 100);
}
