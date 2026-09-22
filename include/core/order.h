#pragma once

#include <chrono>

enum class Side
{
    BUY,
    SELL
};

enum class OrderType
{
    LIMIT,
    MARKET,
    IOC,
    FOK
};

class Order
{
public:
    int order_id;
    double price;
    int quantity;
    Side side;
    OrderType type;

    std::chrono::high_resolution_clock::time_point timestamp;

    Order(
        int id,
        double p,
        int qty,
        Side s,
        OrderType t = OrderType::LIMIT);
};