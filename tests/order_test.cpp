#include <gtest/gtest.h>

#include "core/order.h"

TEST(OrderTest, ConstructsWithGivenFields)
{
    Order order(1, 100.5, 10, Side::BUY);

    EXPECT_EQ(order.order_id, 1);
    EXPECT_DOUBLE_EQ(order.price, 100.5);
    EXPECT_EQ(order.quantity, 10);
    EXPECT_EQ(order.side, Side::BUY);
}

TEST(OrderTest, PreservesSellSide)
{
    Order order(2, 50.0, 5, Side::SELL);

    EXPECT_EQ(order.side, Side::SELL);
}

TEST(OrderTest, TimestampIsSetAtConstructionTime)
{
    auto before = std::chrono::high_resolution_clock::now();
    Order order(3, 10.0, 1, Side::BUY);
    auto after = std::chrono::high_resolution_clock::now();

    EXPECT_GE(order.timestamp, before);
    EXPECT_LE(order.timestamp, after);
}

TEST(OrderTest, ThrowsOnZeroPrice)
{
    EXPECT_THROW(Order(4, 0.0, 10, Side::BUY), std::invalid_argument);
}

TEST(OrderTest, ThrowsOnNegativePrice)
{
    EXPECT_THROW(Order(5, -1.0, 10, Side::BUY), std::invalid_argument);
}

TEST(OrderTest, ThrowsOnZeroQuantity)
{
    EXPECT_THROW(Order(6, 100.0, 0, Side::BUY), std::invalid_argument);
}

TEST(OrderTest, ThrowsOnNegativeQuantity)
{
    EXPECT_THROW(Order(7, 100.0, -5, Side::BUY), std::invalid_argument);
}

TEST(OrderTest, DefaultsToLimitTypeWhenUnspecified)
{
    Order order(8, 100.0, 10, Side::BUY);

    EXPECT_EQ(order.type, OrderType::LIMIT);
}

TEST(OrderTest, PreservesExplicitOrderType)
{
    Order market(9, 100.0, 10, Side::BUY, OrderType::MARKET);
    Order ioc(10, 100.0, 10, Side::BUY, OrderType::IOC);
    Order fok(11, 100.0, 10, Side::BUY, OrderType::FOK);

    EXPECT_EQ(market.type, OrderType::MARKET);
    EXPECT_EQ(ioc.type, OrderType::IOC);
    EXPECT_EQ(fok.type, OrderType::FOK);
}
