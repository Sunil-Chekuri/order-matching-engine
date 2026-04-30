#include <iostream>
#include "order.h"

int main()
{
    Order order1(
        1,
        100.5,
        10,
        Side::BUY
    );

    std::cout
        << "Order created with ID: "
        << order1.order_id
        << std::endl;

    return 0;
}