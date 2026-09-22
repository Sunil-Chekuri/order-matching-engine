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

// ---- Quantity bookkeeping audit for aggressive order types (Day 6) ----

TEST(OrderTypesTest, MarketOrderLeavesExactRemainingQuantityOnRestingSide)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::SELL));

    engine.processOrder(Order(2, 1.0, 4, Side::BUY, OrderType::MARKET));

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 6);
}

TEST(OrderTypesTest, IocLeavesExactRemainingQuantityOnRestingSide)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::SELL));

    engine.processOrder(Order(2, 100.0, 4, Side::BUY, OrderType::IOC));

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 6);
}

TEST(OrderTypesTest, FokLeavesExactRemainingQuantityOnLastPartiallyConsumedLevel)
{
    // A successful FOK can still only partially consume the final
    // resting order it touches once the incoming quantity is exhausted.
    MatchingEngine engine;
    engine.processOrder(Order(1, 99.0, 5, Side::SELL));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    engine.processOrder(Order(3, 100.0, 10, Side::BUY, OrderType::FOK));

    EXPECT_EQ(engine.getTotalTrades(), 2);

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(2, qty));
    EXPECT_EQ(qty, 5); // 10 - (10 - 5 already taken from order 1)
}

// ---- Self-trade prevention for aggressive order types (Day 6) ----

TEST(OrderTypesTest, MarketOrderSelfTradeIsBlocked)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::SELL, OrderType::LIMIT, 1));

    engine.processOrder(Order(2, 1.0, 10, Side::BUY, OrderType::MARKET, 1));

    EXPECT_EQ(engine.getTotalTrades(), 0);

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 10);
}

TEST(OrderTypesTest, IocSelfTradeIsBlocked)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::SELL, OrderType::LIMIT, 1));

    engine.processOrder(Order(2, 100.0, 10, Side::BUY, OrderType::IOC, 1));

    EXPECT_EQ(engine.getTotalTrades(), 0);

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 10);
}

TEST(OrderTypesTest, FokPreCheckAccountsForSelfTradeBlockingBetterPricedLevel)
{
    // Regression test for an interaction bug between self-trade
    // prevention and FOK's all-or-nothing pre-check: order 1 (the best
    // price) belongs to the same participant as the incoming FOK, so
    // matching will stop immediately upon reaching it and order 2 is
    // never actually reachable. A pre-check that naively summed *all*
    // crossing quantity regardless of participant would see 5 + 10 = 15
    // available (enough for the FOK's 10), let the order proceed, and
    // then it would fill 0 units when matching immediately hits the
    // self-order and stops. availableToMatch must stop counting at the
    // same self-order matching would actually stop at, so the pre-check
    // correctly reports 0 available and the FOK is killed outright.
    MatchingEngine engine;
    engine.processOrder(Order(1, 99.0, 5, Side::SELL, OrderType::LIMIT, 1));  // self
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 2)); // not self

    engine.processOrder(Order(3, 100.0, 10, Side::BUY, OrderType::FOK, 1));

    EXPECT_EQ(engine.getTotalTrades(), 0);

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 5);
    ASSERT_TRUE(engine.getRemainingQuantity(2, qty));
    EXPECT_EQ(qty, 10);
}
