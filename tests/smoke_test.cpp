#include <gtest/gtest.h>

#include "core/order.h"

TEST(Smoke, OrderConstructsWithGivenFields)
{
    Order order(1, 100.5, 10, Side::BUY);

    EXPECT_EQ(order.order_id, 1);
    EXPECT_DOUBLE_EQ(order.price, 100.5);
    EXPECT_EQ(order.quantity, 10);
    EXPECT_EQ(order.side, Side::BUY);
}
