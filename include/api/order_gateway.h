#pragma once

#include <cstddef>
#include <string>

#include "engine/matching_engine.h"

// The in-process entry point to the engine, and the only surface the
// network layer is allowed to touch.
//
// Everything here is safe to call from several threads at once: the
// engine shards per symbol and locks each shard for the whole of an
// operation, so one connection thread per client needs no locking of
// its own.
class OrderGateway
{
private:
    MatchingEngine engine;

public:
    void submitOrder(const Order &order);

    // The symbol is defaulted so existing single-instrument callers
    // (main.cpp's benchmark) keep compiling, but the engine genuinely
    // needs it: there is no global order-id index to look the symbol up
    // in, by design — see MatchingEngine::cancelOrder.
    bool cancelOrder(
        int order_id,
        const std::string &symbol = DEFAULT_SYMBOL);

    // How much of a resting order is still live. False if no such order
    // is resting on that symbol — which includes an order that has
    // already been filled or cancelled.
    bool getRemainingQuantity(
        int order_id,
        int &out_quantity,
        const std::string &symbol = DEFAULT_SYMBOL);

    // Aggregated top-N depth. This is the read side a dashboard or a
    // market-data consumer wants, and it is L2 by construction — it
    // never exposes individual resting order ids.
    BookSnapshot snapshot(
        std::size_t depth,
        const std::string &symbol = DEFAULT_SYMBOL);

    // Exposed once there was an actual caller for it (the metrics line
    // main.cpp emits), rather than speculatively when metrics were added.
    EngineMetrics metrics() const;
};
