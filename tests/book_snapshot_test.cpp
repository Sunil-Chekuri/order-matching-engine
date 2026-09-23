#include <gtest/gtest.h>

#include "core/order.h"
#include "engine/matching_engine.h"
#include "engine/order_book.h"

TEST(BookSnapshotTest, EmptyBookProducesEmptySnapshot)
{
    OrderBook book;

    BookSnapshot snapshot = book.snapshot(5);

    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(BookSnapshotTest, ZeroDepthProducesEmptySnapshot)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    book.addOrder(Order(2, 101.0, 10, Side::SELL));

    BookSnapshot snapshot = book.snapshot(0);

    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(BookSnapshotTest, SingleOrderProducesSingleLevel)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 7, Side::BUY));

    BookSnapshot snapshot = book.snapshot(5);

    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 100.0);
    EXPECT_EQ(snapshot.bids[0].total_quantity, 7);
    EXPECT_EQ(snapshot.bids[0].order_count, 1);
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(BookSnapshotTest, OrdersAtSamePriceAggregateIntoOneLevel)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 5, Side::BUY));
    book.addOrder(Order(2, 100.0, 3, Side::BUY));
    book.addOrder(Order(3, 100.0, 2, Side::BUY));

    BookSnapshot snapshot = book.snapshot(5);

    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_EQ(snapshot.bids[0].total_quantity, 10);
    EXPECT_EQ(snapshot.bids[0].order_count, 3);
}

TEST(BookSnapshotTest, BidsAreOrderedBestPriceFirst)
{
    OrderBook book;
    book.addOrder(Order(1, 99.0, 10, Side::BUY));
    book.addOrder(Order(2, 101.0, 10, Side::BUY));
    book.addOrder(Order(3, 100.0, 10, Side::BUY));

    BookSnapshot snapshot = book.snapshot(5);

    ASSERT_EQ(snapshot.bids.size(), 3u);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 101.0);
    EXPECT_DOUBLE_EQ(snapshot.bids[1].price, 100.0);
    EXPECT_DOUBLE_EQ(snapshot.bids[2].price, 99.0);
}

TEST(BookSnapshotTest, AsksAreOrderedBestPriceFirst)
{
    OrderBook book;
    book.addOrder(Order(1, 103.0, 10, Side::SELL));
    book.addOrder(Order(2, 101.0, 10, Side::SELL));
    book.addOrder(Order(3, 102.0, 10, Side::SELL));

    BookSnapshot snapshot = book.snapshot(5);

    ASSERT_EQ(snapshot.asks.size(), 3u);
    EXPECT_DOUBLE_EQ(snapshot.asks[0].price, 101.0);
    EXPECT_DOUBLE_EQ(snapshot.asks[1].price, 102.0);
    EXPECT_DOUBLE_EQ(snapshot.asks[2].price, 103.0);
}

TEST(BookSnapshotTest, DepthLimitsLevelsReturnedToTheBestOnes)
{
    OrderBook book;
    for (int i = 0; i < 10; ++i)
        book.addOrder(Order(i + 1, 100.0 - i, 10, Side::BUY));

    BookSnapshot snapshot = book.snapshot(3);

    ASSERT_EQ(snapshot.bids.size(), 3u);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 100.0);
    EXPECT_DOUBLE_EQ(snapshot.bids[1].price, 99.0);
    EXPECT_DOUBLE_EQ(snapshot.bids[2].price, 98.0);
}

TEST(BookSnapshotTest, DepthBeyondAvailableLevelsIsNotPadded)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    book.addOrder(Order(2, 99.0, 10, Side::BUY));

    BookSnapshot snapshot = book.snapshot(50);

    EXPECT_EQ(snapshot.bids.size(), 2u);
}

TEST(BookSnapshotTest, BothSidesArePopulatedIndependently)
{
    OrderBook book;
    book.addOrder(Order(1, 99.0, 4, Side::BUY));
    book.addOrder(Order(2, 101.0, 6, Side::SELL));
    book.addOrder(Order(3, 102.0, 8, Side::SELL));

    BookSnapshot snapshot = book.snapshot(5);

    ASSERT_EQ(snapshot.bids.size(), 1u);
    ASSERT_EQ(snapshot.asks.size(), 2u);
    EXPECT_EQ(snapshot.bids[0].total_quantity, 4);
    EXPECT_EQ(snapshot.asks[0].total_quantity, 6);
    EXPECT_EQ(snapshot.asks[1].total_quantity, 8);
}

TEST(BookSnapshotTest, SnapshotDoesNotMutateTheBook)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    book.addOrder(Order(2, 99.0, 5, Side::BUY));

    BookSnapshot first = book.snapshot(5);
    BookSnapshot second = book.snapshot(5);

    ASSERT_EQ(first.bids.size(), second.bids.size());
    EXPECT_DOUBLE_EQ(first.bids[0].price, second.bids[0].price);
    EXPECT_EQ(first.bids[0].total_quantity, second.bids[0].total_quantity);

    // The orders themselves must still be resting and cancellable.
    EXPECT_TRUE(book.cancelOrder(1));
}

TEST(BookSnapshotTest, SnapshotReflectsRemainderAfterPartialFill)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 4, Side::SELL));

    BookSnapshot snapshot = engine.snapshot(5);

    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_EQ(snapshot.bids[0].total_quantity, 6);
    EXPECT_EQ(snapshot.bids[0].order_count, 1);
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(BookSnapshotTest, FullyFilledLevelDisappearsFromSnapshot)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    BookSnapshot snapshot = engine.snapshot(5);

    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(BookSnapshotTest, CancelledOrderLeavesTheSnapshot)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 99.0, 10, Side::BUY));

    ASSERT_TRUE(engine.cancelOrder(1));

    BookSnapshot snapshot = engine.snapshot(5);

    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 99.0);
}

TEST(BookSnapshotTest, AggregationHidesIndividualOrdersButPreservesTheirTotal)
{
    // Two orders of 5 and one order of 10 must be indistinguishable in
    // total from the quantity side, but distinguishable by order_count —
    // that count is the only thing an L2 feed reveals about composition.
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 5, Side::BUY));
    engine.processOrder(Order(2, 100.0, 5, Side::BUY));

    BookSnapshot snapshot = engine.snapshot(1);

    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_EQ(snapshot.bids[0].total_quantity, 10);
    EXPECT_EQ(snapshot.bids[0].order_count, 2);
}
