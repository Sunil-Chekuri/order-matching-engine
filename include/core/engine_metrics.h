#pragma once

#include <cstddef>
#include <string>

// A point-in-time reading of what the engine has done and is holding.
//
// Counters are cumulative since start; resting_orders is a gauge. Rates
// (orders/sec and so on) are deliberately not stored here — they are a
// property of two readings and the time between them, not of one.
struct EngineMetrics
{
    long long orders_submitted = 0;

    long long trades_executed = 0;

    long long cancels_accepted = 0;

    long long cancels_rejected = 0;

    // The "queue depth" of this engine: orders currently resting across
    // every book, waiting for a counterparty.
    long long resting_orders = 0;

    std::size_t symbols = 0;
};

// Machine-readable single-line rendering, so a scraper or log pipeline
// can consume readings without parsing prose.
std::string toJsonLine(
    const EngineMetrics &metrics);
