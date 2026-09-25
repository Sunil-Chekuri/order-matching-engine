#include "api/order_gateway.h"

void OrderGateway::submitOrder(
    const Order &order)
{
    engine.processOrder(order);
}

bool OrderGateway::cancelOrder(
    int order_id,
    const std::string &symbol)
{
    return engine.cancelOrder(
        order_id,
        symbol);
}

bool OrderGateway::getRemainingQuantity(
    int order_id,
    int &out_quantity,
    const std::string &symbol)
{
    return engine.getRemainingQuantity(
        order_id,
        out_quantity,
        symbol);
}

BookSnapshot OrderGateway::snapshot(
    std::size_t depth,
    const std::string &symbol)
{
    return engine.snapshot(
        depth,
        symbol);
}

EngineMetrics OrderGateway::metrics() const
{
    return engine.metrics();
}
