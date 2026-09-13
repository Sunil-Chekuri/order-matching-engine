#include "api/order_gateway.h"

void OrderGateway::submitOrder(
    const Order &order)
{
    engine.processOrder(order);
}

bool OrderGateway::cancelOrder(
    int order_id)
{
    return engine.cancelOrder(
        order_id);
}