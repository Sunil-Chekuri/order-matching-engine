#pragma once

#include <chrono>

enum class Side
{
    BUY,
    SELL
};

class Order
{
public:
    int order_id;
    double price;
    int quantity;
    Side side;

    std::chrono::high_resolution_clock::time_point timestamp;

    Order(
        int id,
        double p,
        int qty,
        Side s);
};