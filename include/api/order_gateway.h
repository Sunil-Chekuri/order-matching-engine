#pragma once

#include "engine/matching_engine.h"

class OrderGateway
{
private:
    MatchingEngine engine;

public:
    void submitOrder(const Order &order);

    bool cancelOrder(int order_id);
};