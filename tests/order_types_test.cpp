#include <gtest/gtest.h>

#include "core/order.h"
#include "engine/matching_engine.h"

// MARKET, IOC, and FOK orders never rest in the book, so — same as
// Day 4's MatchingEngine tests — cancelOrder() doubles as the
// observability hook: false always means "not resting," whether that's
// because it never existed, or because it (or its remainder) was
// discarded by design after these aggressive order types executed.

// ---- MARKET ----

TEST(OrderTypesTest, MarketOrderMatchesAvailableLiquidityIgnoringPrice)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 150.0, 5, Side::SELL)); // far from any "reasonable" price

    engine.processOrder(Order(2, 1.0, 5, Side::BUY, OrderType::MARKET));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(1)); // resting sell fully filled
    EXPECT_FALSE(engine.cancelOrder(2)); // market order never rests
}

TEST(OrderTypesTest, MarketOrderSweepsMultiplePriceLevels)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 5, Side::SELL));
    engine.processOrder(Order(2, 105.0, 5, Side::SELL));

    engine.processOrder(Order(3, 1.0, 8, Side::BUY, OrderType::MARKET));

    EXPECT_EQ(engine.getTotalTrades(), 2);
    EXPECT_FALSE(engine.cancelOrder(1)); // best price consumed first, fully filled
    EXPECT_TRUE(engine.cancelOrder(2));  // partially filled, 2 remaining, still resting
    EXPECT_FALSE(engine.cancelOrder(3)); // market order never rests
}

TEST(OrderTypesTest, MarketOrderRemainderIsDiscardedNotRested)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 3, Side::SELL));

    engine.processOrder(Order(2, 1.0, 10, Side::BUY, OrderType::MARKET));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(2)); // remaining 7 units simply vanish
}

TEST(OrderTypesTest, MarketOrderWithNoLiquidityProducesNoTrades)
{
    MatchingEngine engine;

    engine.processOrder(Order(1, 1.0, 10, Side::BUY, OrderType::MARKET));

    EXPECT_EQ(engine.getTotalTrades(), 0);
    EXPECT_FALSE(engine.cancelOrder(1));
}

// ---- IOC ----

TEST(OrderTypesTest, IocFillsWhatCrossesAndDiscardsRemainder)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 99.0, 5, Side::SELL));
    engine.processOrder(Order(2, 101.0, 5, Side::SELL)); // does not cross IOC's limit

    engine.processOrder(Order(3, 100.0, 8, Side::BUY, OrderType::IOC));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(1)); // crossed, fully filled
    EXPECT_TRUE(engine.cancelOrder(2));  // never crossed, untouched
    EXPECT_FALSE(engine.cancelOrder(3)); // IOC never rests, remainder discarded
}

TEST(OrderTypesTest, IocWithNoCrossingLiquidityProducesNoTrades)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 105.0, 5, Side::SELL));

    engine.processOrder(Order(2, 100.0, 5, Side::BUY, OrderType::IOC));

    EXPECT_EQ(engine.getTotalTrades(), 0);
    EXPECT_TRUE(engine.cancelOrder(1));  // untouched
    EXPECT_FALSE(engine.cancelOrder(2)); // IOC never rests
}

TEST(OrderTypesTest, IocCanFullyFillWithNoRemainder)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 5, Side::SELL));

    engine.processOrder(Order(2, 100.0, 5, Side::BUY, OrderType::IOC));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(2));
}

// ---- FOK ----

TEST(OrderTypesTest, FokExecutesFullyWhenEnoughLiquidityAvailable)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 99.0, 5, Side::SELL));
    engine.processOrder(Order(2, 100.0, 5, Side::SELL));

    engine.processOrder(Order(3, 100.0, 10, Side::BUY, OrderType::FOK));

    EXPECT_EQ(engine.getTotalTrades(), 2);
    EXPECT_FALSE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(2));
    EXPECT_FALSE(engine.cancelOrder(3)); // FOK never rests
}

TEST(OrderTypesTest, FokKillsEntireOrderWhenLiquidityInsufficient)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 5, Side::SELL));

    engine.processOrder(Order(2, 100.0, 10, Side::BUY, OrderType::FOK));

    EXPECT_EQ(engine.getTotalTrades(), 0);   // no partial fill at all
    EXPECT_TRUE(engine.cancelOrder(1));      // untouched by the failed FOK
    EXPECT_FALSE(engine.cancelOrder(2));     // FOK never rests, even after failing
}

TEST(OrderTypesTest, FokIgnoresLiquidityThatDoesNotCrossItsLimit)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 5, Side::SELL));
    engine.processOrder(Order(2, 110.0, 10, Side::SELL)); // too expensive to count

    engine.processOrder(Order(3, 100.0, 10, Side::BUY, OrderType::FOK));

    EXPECT_EQ(engine.getTotalTrades(), 0);
    EXPECT_TRUE(engine.cancelOrder(1));
    EXPECT_TRUE(engine.cancelOrder(2));
}
