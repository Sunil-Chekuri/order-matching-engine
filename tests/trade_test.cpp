#include <gtest/gtest.h>

#include "core/trade.h"

TEST(TradeTest, ConstructsWithGivenFields)
{
    Trade trade(1, 10, 20, 99.5, 3);

    EXPECT_EQ(trade.trade_id, 1);
    EXPECT_EQ(trade.buy_order_id, 10);
    EXPECT_EQ(trade.sell_order_id, 20);
    EXPECT_DOUBLE_EQ(trade.price, 99.5);
    EXPECT_EQ(trade.quantity, 3);
}

TEST(TradeTest, ThrowsOnZeroPrice)
{
    EXPECT_THROW(Trade(2, 10, 20, 0.0, 3), std::invalid_argument);
}

TEST(TradeTest, ThrowsOnNegativePrice)
{
    EXPECT_THROW(Trade(3, 10, 20, -5.0, 3), std::invalid_argument);
}

TEST(TradeTest, ThrowsOnZeroQuantity)
{
    EXPECT_THROW(Trade(4, 10, 20, 99.5, 0), std::invalid_argument);
}

TEST(TradeTest, ThrowsOnNegativeQuantity)
{
    EXPECT_THROW(Trade(5, 10, 20, 99.5, -3), std::invalid_argument);
}
