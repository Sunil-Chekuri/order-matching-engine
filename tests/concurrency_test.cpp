#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/order.h"
#include "engine/matching_engine.h"

namespace
{
    const int THREAD_COUNT = 4;

    const int PAIRS_PER_THREAD = 500;
}

TEST(ConcurrencyTest, ConcurrentMatchingPairsProduceADeterministicOutcome)
{
    // Every order here is quantity 10 at the same price, so each match
    // consumes exactly one buy and one sell in full. The interleaving of
    // threads is non-deterministic, but the outcome is not: with equal
    // numbers of buys and sells submitted, the book must end empty and
    // the trade count must equal the number of pairs. Anything else
    // means quantities or removals were lost to a race.
    MatchingEngine engine;

    std::vector<std::thread> workers;

    for (int t = 0; t < THREAD_COUNT; ++t)
    {
        workers.emplace_back(
            [&engine, t]()
            {
                int base = t * 1000000 + 1;

                for (int i = 0; i < PAIRS_PER_THREAD; ++i)
                {
                    engine.processOrder(
                        Order(base + i * 2, 100.0, 10, Side::BUY));

                    engine.processOrder(
                        Order(base + i * 2 + 1, 100.0, 10, Side::SELL));
                }
            });
    }

    for (auto &worker : workers)
        worker.join();

    EXPECT_EQ(engine.getTotalTrades(), THREAD_COUNT * PAIRS_PER_THREAD);

    BookSnapshot snapshot = engine.snapshot(10);
    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(ConcurrencyTest, SnapshotsTakenDuringWritesAreInternallyConsistent)
{
    // A snapshot must never observe a half-applied mutation. If the read
    // were not serialised against writers it could catch a price level
    // mid-update and report a negative or zero quantity, or levels out
    // of order.
    MatchingEngine engine;

    const int required_snapshots = 200;

    std::atomic<bool> reader_finished{false};
    std::atomic<int> snapshots_taken{0};
    std::atomic<bool> malformed{false};

    // The writer runs until the reader has taken its quota rather than
    // for a fixed number of orders, so the two are guaranteed to overlap.
    // Gating the reader on "is the writer still going" instead made this
    // test flaky: under load the writer could finish before the reader
    // was ever scheduled, and the reader would take zero snapshots.
    std::thread writer(
        [&engine, &reader_finished]()
        {
            int i = 0;

            while (!reader_finished)
            {
                ++i;

                engine.processOrder(
                    Order(i, 100.0 + (i % 20), 10, Side::BUY));
            }
        });

    std::thread reader(
        [&engine, &reader_finished, &snapshots_taken, &malformed, required_snapshots]()
        {
            for (int n = 0; n < required_snapshots; ++n)
            {
                BookSnapshot snapshot = engine.snapshot(10);

                for (std::size_t i = 0; i < snapshot.bids.size(); ++i)
                {
                    if (snapshot.bids[i].total_quantity <= 0)
                        malformed = true;

                    if (snapshot.bids[i].order_count <= 0)
                        malformed = true;

                    // Bids must always be strictly descending in price.
                    if (i > 0 && snapshot.bids[i].price >= snapshot.bids[i - 1].price)
                        malformed = true;
                }

                ++snapshots_taken;
            }

            reader_finished = true;
        });

    reader.join();
    writer.join();

    EXPECT_FALSE(malformed);
    EXPECT_EQ(snapshots_taken.load(), required_snapshots);
}

TEST(ConcurrencyTest, ConcurrentCancelsNeverDoubleCancelTheSameOrder)
{
    // Two threads race to cancel the same set of orders. cancelOrder
    // returning true means "this call is the one that removed it", so
    // across both threads each order must be cancelled exactly once.
    MatchingEngine engine;

    const int order_count = 1000;

    for (int i = 0; i < order_count; ++i)
        engine.processOrder(Order(i + 1, 100.0, 10, Side::BUY));

    std::atomic<int> successful_cancels{0};

    std::vector<std::thread> workers;

    for (int t = 0; t < 2; ++t)
    {
        workers.emplace_back(
            [&engine, &successful_cancels]()
            {
                for (int i = 0; i < order_count; ++i)
                {
                    if (engine.cancelOrder(i + 1))
                        ++successful_cancels;
                }
            });
    }

    for (auto &worker : workers)
        worker.join();

    EXPECT_EQ(successful_cancels.load(), order_count);

    BookSnapshot snapshot = engine.snapshot(5);
    EXPECT_TRUE(snapshot.bids.empty());
}
