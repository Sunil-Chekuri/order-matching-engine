#include <gtest/gtest.h>

#include "core/order.h"
#include "engine/matching_engine.h"

// MatchingEngine deliberately doesn't expose its internal OrderBook, so
// these tests verify behavior only through its public interface:
// processOrder(), cancelOrder(), and the trade counters. cancelOrder()
// doubles as an observability hook here — if it returns true, the order
// is still resting in the book; if false, it was either never known or
// has already been fully matched away.

TEST(MatchingEngineTest, OrderRestsWhenNoMatch)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_EQ(engine.getTotalTrades(), 0);
    EXPECT_TRUE(engine.cancelOrder(1));
}

TEST(MatchingEngineTest, NonCrossingOrdersBothRest)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 99.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 0);
    EXPECT_TRUE(engine.cancelOrder(1));
    EXPECT_TRUE(engine.cancelOrder(2));
}

TEST(MatchingEngineTest, FullFillRemovesBothOrders)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(2));
}

TEST(MatchingEngineTest, PartialFillLeavesLargerBuyResting)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 4, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_TRUE(engine.cancelOrder(1));   // buy partially filled, 6 remaining
    EXPECT_FALSE(engine.cancelOrder(2));  // sell fully filled
}

TEST(MatchingEngineTest, PartialFillLeavesLargerSellResting)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 4, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(1));  // buy fully filled
    EXPECT_TRUE(engine.cancelOrder(2));   // sell partially filled, 6 remaining
}

TEST(MatchingEngineTest, PriceTimePriorityFillsEarlierOrderFirst)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 5, Side::BUY));
    engine.processOrder(Order(2, 100.0, 5, Side::BUY));
    engine.processOrder(Order(3, 100.0, 5, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 1);
    EXPECT_FALSE(engine.cancelOrder(1)); // earlier order filled first
    EXPECT_TRUE(engine.cancelOrder(2));  // later order still resting
    EXPECT_FALSE(engine.cancelOrder(3)); // sell fully filled
}

TEST(MatchingEngineTest, MatchesAcrossMultiplePriceLevelsBestPriceFirst)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 98.0, 5, Side::BUY));
    engine.processOrder(Order(2, 100.0, 5, Side::BUY));
    engine.processOrder(Order(3, 99.0, 5, Side::BUY));

    engine.processOrder(Order(4, 99.0, 10, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 2);
    EXPECT_FALSE(engine.cancelOrder(2)); // best price (100) filled first
    EXPECT_FALSE(engine.cancelOrder(3)); // next best (99) filled second
    EXPECT_TRUE(engine.cancelOrder(1));  // 98 never reached, sell exhausted first
    EXPECT_FALSE(engine.cancelOrder(4)); // sell fully filled
}

TEST(MatchingEngineTest, CancelOrderReturnsFalseForUnknownId)
{
    MatchingEngine engine;

    EXPECT_FALSE(engine.cancelOrder(42));
}

TEST(MatchingEngineTest, CancelOrderCannotBeCancelledTwice)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_TRUE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(1));
}

TEST(MatchingEngineTest, CancelAfterFullFillDoesNotCrashOrSucceed)
{
    // Regression test for a dangling order_registry pointer: previously,
    // removeBestBid/removeBestAsk popped a filled order out of the book
    // without erasing it from order_registry, so cancelling an
    // already-filled order dereferenced a dangling pointer (UB) instead
    // of safely returning false.
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    ASSERT_EQ(engine.getTotalTrades(), 1);

    EXPECT_FALSE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(2));
}

TEST(MatchingEngineTest, TradeCounterAndTotalTradesStayInSync)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));
    engine.processOrder(Order(3, 101.0, 5, Side::BUY));
    engine.processOrder(Order(4, 101.0, 5, Side::SELL));

    EXPECT_EQ(engine.getTradeCounter(), engine.getTotalTrades());
    EXPECT_EQ(engine.getTotalTrades(), 2);
}

// ---- Quantity bookkeeping audit (Day 6) ----
// Prior partial-fill tests only checked whether an order was still
// resting (via cancelOrder's true/false), never the exact leftover
// quantity. A bug that decremented quantity by the wrong amount would
// have slipped past all of them. These use getRemainingQuantity to
// check the actual numbers.

TEST(MatchingEngineTest, PartialFillLeavesExactRemainingQuantityOnBuy)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 4, Side::SELL));

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 6);
}

TEST(MatchingEngineTest, PartialFillLeavesExactRemainingQuantityOnSell)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 4, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(2, qty));
    EXPECT_EQ(qty, 6);
}

TEST(MatchingEngineTest, MultipleSequentialPartialFillsAccumulateCorrectly)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 20, Side::BUY));
    engine.processOrder(Order(2, 100.0, 3, Side::SELL));
    engine.processOrder(Order(3, 100.0, 5, Side::SELL));
    engine.processOrder(Order(4, 100.0, 2, Side::SELL));

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 10); // 20 - 3 - 5 - 2
    EXPECT_EQ(engine.getTotalTrades(), 3);
}

// ---- Self-trade prevention (Day 6) ----

TEST(MatchingEngineTest, SelfTradeIsBlockedForLimitOrders)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 1));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 1));

    EXPECT_EQ(engine.getTotalTrades(), 0);

    int qty = -1;
    ASSERT_TRUE(engine.getRemainingQuantity(1, qty));
    EXPECT_EQ(qty, 10);
    ASSERT_TRUE(engine.getRemainingQuantity(2, qty));
    EXPECT_EQ(qty, 10);
}

TEST(MatchingEngineTest, DifferentParticipantsStillMatchNormally)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 1));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 2));

    EXPECT_EQ(engine.getTotalTrades(), 1);
}

TEST(MatchingEngineTest, UnspecifiedParticipantsNeverTriggerSelfTradeBlock)
{
    // Both orders default participant_id to 0 ("unspecified"); 0 must
    // never be treated as a shared identity between two such orders.
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_EQ(engine.getTotalTrades(), 1);
}
