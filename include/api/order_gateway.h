#pragma once

#include "engine/matching_engine.h"

class OrderGateway
{
private:
    MatchingEngine engine;

public:
    void submitOrder(const Order &order);

    bool cancelOrder(int order_id);

    // Exposed once there was an actual caller for it (the metrics line
    // main.cpp emits), rather than speculatively when metrics were added.
    EngineMetrics metrics() const;
};