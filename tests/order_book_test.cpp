#include <gtest/gtest.h>

#include "core/order.h"
#include "engine/order_book.h"

TEST(OrderBookTest, HasMatchIsFalseOnEmptyBook)
{
    OrderBook book;

    EXPECT_FALSE(book.hasMatch());
}

TEST(OrderBookTest, HasMatchIsFalseWithOnlyBids)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_FALSE(book.hasMatch());
}

TEST(OrderBookTest, HasMatchIsFalseWithOnlyAsks)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::SELL));

    EXPECT_FALSE(book.hasMatch());
}

TEST(OrderBookTest, HasMatchIsFalseWhenBidBelowAsk)
{
    OrderBook book;
    book.addOrder(Order(1, 99.0, 10, Side::BUY));
    book.addOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_FALSE(book.hasMatch());
}

TEST(OrderBookTest, HasMatchIsTrueWhenBidMeetsAsk)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    book.addOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_TRUE(book.hasMatch());
}

TEST(OrderBookTest, HasMatchIsTrueWhenBidCrossesAsk)
{
    OrderBook book;
    book.addOrder(Order(1, 105.0, 10, Side::BUY));
    book.addOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_TRUE(book.hasMatch());
}

TEST(OrderBookTest, BestBidThrowsOnEmptyBook)
{
    OrderBook book;

    EXPECT_THROW(book.bestBid(), std::out_of_range);
}

TEST(OrderBookTest, BestAskThrowsOnEmptyBook)
{
    OrderBook book;

    EXPECT_THROW(book.bestAsk(), std::out_of_range);
}

TEST(OrderBookTest, RemoveBestBidThrowsOnEmptyBook)
{
    OrderBook book;

    EXPECT_THROW(book.removeBestBid(), std::out_of_range);
}

TEST(OrderBookTest, RemoveBestAskThrowsOnEmptyBook)
{
    OrderBook book;

    EXPECT_THROW(book.removeBestAsk(), std::out_of_range);
}

TEST(OrderBookTest, BestBidReturnsHighestPriceOrder)
{
    OrderBook book;
    book.addOrder(Order(1, 99.0, 10, Side::BUY));
    book.addOrder(Order(2, 101.0, 10, Side::BUY));
    book.addOrder(Order(3, 100.0, 10, Side::BUY));

    EXPECT_EQ(book.bestBid().order_id, 2);
}

TEST(OrderBookTest, BestAskReturnsLowestPriceOrder)
{
    OrderBook book;
    book.addOrder(Order(1, 105.0, 10, Side::SELL));
    book.addOrder(Order(2, 101.0, 10, Side::SELL));
    book.addOrder(Order(3, 103.0, 10, Side::SELL));

    EXPECT_EQ(book.bestAsk().order_id, 2);
}

TEST(OrderBookTest, FifoOrderPreservedAtSamePriceLevel)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    book.addOrder(Order(2, 100.0, 10, Side::BUY));
    book.addOrder(Order(3, 100.0, 10, Side::BUY));

    EXPECT_EQ(book.bestBid().order_id, 1);

    book.removeBestBid();
    EXPECT_EQ(book.bestBid().order_id, 2);

    book.removeBestBid();
    EXPECT_EQ(book.bestBid().order_id, 3);
}

TEST(OrderBookTest, RemoveBestBidRemovesPriceLevelWhenLastOrderPopped)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));

    book.removeBestBid();

    EXPECT_THROW(book.bestBid(), std::out_of_range);
}

TEST(OrderBookTest, RemoveBestAskRemovesPriceLevelWhenLastOrderPopped)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::SELL));

    book.removeBestAsk();

    EXPECT_THROW(book.bestAsk(), std::out_of_range);
}

TEST(OrderBookTest, CancelOrderReturnsFalseForUnknownId)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_FALSE(book.cancelOrder(999));
}

TEST(OrderBookTest, CancelOrderRemovesBuyOrder)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_THROW(book.bestBid(), std::out_of_range);
}

TEST(OrderBookTest, CancelOrderRemovesSellOrder)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::SELL));

    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_THROW(book.bestAsk(), std::out_of_range);
}

TEST(OrderBookTest, CancelOrderCannotBeCancelledTwice)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(1));
}

TEST(OrderBookTest, CancelOrderPreservesFifoForRemainingOrders)
{
    OrderBook book;
    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    book.addOrder(Order(2, 100.0, 10, Side::BUY));
    book.addOrder(Order(3, 100.0, 10, Side::BUY));

    EXPECT_TRUE(book.cancelOrder(2));

    EXPECT_EQ(book.bestBid().order_id, 1);

    book.removeBestBid();
    EXPECT_EQ(book.bestBid().order_id, 3);
}

TEST(OrderBookTest, CancelOrderLeavesOtherPriceLevelsIntact)
{
    OrderBook book;
    book.addOrder(Order(1, 99.0, 10, Side::BUY));
    book.addOrder(Order(2, 100.0, 10, Side::BUY));

    EXPECT_TRUE(book.cancelOrder(2));

    EXPECT_EQ(book.bestBid().order_id, 1);
}

TEST(OrderBookTest, HasBidsAndHasAsksReflectBookState)
{
    OrderBook book;

    EXPECT_FALSE(book.hasBids());
    EXPECT_FALSE(book.hasAsks());

    book.addOrder(Order(1, 100.0, 10, Side::BUY));
    EXPECT_TRUE(book.hasBids());
    EXPECT_FALSE(book.hasAsks());

    book.addOrder(Order(2, 100.0, 10, Side::SELL));
    EXPECT_TRUE(book.hasAsks());
}

TEST(OrderBookTest, AvailableToMatchSumsAsksAtOrBelowLimitForIncomingBuy)
{
    OrderBook book;
    book.addOrder(Order(1, 99.0, 5, Side::SELL));
    book.addOrder(Order(2, 100.0, 5, Side::SELL));
    book.addOrder(Order(3, 101.0, 5, Side::SELL));

    EXPECT_EQ(book.availableToMatch(Side::BUY, 100.0), 10);
}

TEST(OrderBookTest, AvailableToMatchSumsBidsAtOrAboveLimitForIncomingSell)
{
    OrderBook book;
    book.addOrder(Order(1, 101.0, 5, Side::BUY));
    book.addOrder(Order(2, 100.0, 5, Side::BUY));
    book.addOrder(Order(3, 99.0, 5, Side::BUY));

    EXPECT_EQ(book.availableToMatch(Side::SELL, 100.0), 10);
}

TEST(OrderBookTest, AvailableToMatchIsZeroWhenNothingCrosses)
{
    OrderBook book;
    book.addOrder(Order(1, 105.0, 5, Side::SELL));

    EXPECT_EQ(book.availableToMatch(Side::BUY, 100.0), 0);
}

TEST(OrderBookTest, AvailableToMatchIsZeroOnEmptyBook)
{
    OrderBook book;

    EXPECT_EQ(book.availableToMatch(Side::BUY, 100.0), 0);
    EXPECT_EQ(book.availableToMatch(Side::SELL, 100.0), 0);
}
