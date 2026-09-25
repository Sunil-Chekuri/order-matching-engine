#include "api/order_gateway.h"
#include "utils/timer.h"
#include "utils/logger.h"
#include "utils/latency_stats.h"

#include <string>

namespace
{
    const int NUM_PAIRS = 10000;

    // Measured without per-order instrumentation: two extra clock reads
    // around every submission would inflate the very total this pass is
    // trying to report.
    void runThroughputPass()
    {
        OrderGateway gateway;

        Timer timer;

        timer.start();

        for (int i = 0; i < NUM_PAIRS; ++i)
        {
            gateway.submitOrder(
                Order(i, 100.0, 10, Side::BUY));

            gateway.submitOrder(
                Order(i + NUM_PAIRS, 100.0, 10, Side::SELL));
        }

        long long elapsed_us = timer.stop();

        int total_orders = NUM_PAIRS * 2;

        Logger::log(
            LogLevel::INFO,
            "Total orders processed: " + std::to_string(total_orders));

        Logger::log(
            LogLevel::INFO,
            "Total latency: " + std::to_string(elapsed_us) + " us");

        double throughput =
            total_orders /
            (static_cast<double>(elapsed_us) / 1e6);

        Logger::log(
            LogLevel::INFO,
            "Throughput: " + std::to_string(throughput) + " orders/sec");
    }

    // Each submission is timed individually so the report describes the
    // distribution rather than one blended average. These samples carry
    // the cost of two clock reads per order, so they read slightly high
    // compared to the throughput pass above.
    //
    // Day 8 ran this twice, with logging live and suppressed, to isolate
    // how much of the tail belonged to the synchronous log write inside
    // the matching loop. Day 13 removed that write entirely — the two
    // passes became statistically indistinguishable — so there is only
    // one pass again.
    void runLatencyPass(
        const std::string &label)
    {
        OrderGateway gateway;

        LatencyStats stats;
        stats.reserve(NUM_PAIRS * 2);

        Timer timer;

        for (int i = 0; i < NUM_PAIRS; ++i)
        {
            timer.start();
            gateway.submitOrder(
                Order(i, 100.0, 10, Side::BUY));
            stats.record(timer.stopNanos());

            timer.start();
            gateway.submitOrder(
                Order(i + NUM_PAIRS, 100.0, 10, Side::SELL));
            stats.record(timer.stopNanos());
        }

        Logger::log(
            LogLevel::INFO,
            label + " samples: " + std::to_string(stats.count()));

        Logger::log(
            LogLevel::INFO,
            label + " min: " + std::to_string(stats.min()) + " ns");

        Logger::log(
            LogLevel::INFO,
            label + " mean: " + std::to_string(stats.mean()) + " ns");

        Logger::log(
            LogLevel::INFO,
            label + " p50: " + std::to_string(stats.percentile(50.0)) + " ns");

        Logger::log(
            LogLevel::INFO,
            label + " p95: " + std::to_string(stats.percentile(95.0)) + " ns");

        Logger::log(
            LogLevel::INFO,
            label + " p99: " + std::to_string(stats.percentile(99.0)) + " ns");

        Logger::log(
            LogLevel::INFO,
            label + " max: " + std::to_string(stats.max()) + " ns");

        // Counters are read once here, off the matching path, and emitted
        // as one machine-readable line. Nothing in the loop above did any
        // I/O of its own.
        Logger::log(
            LogLevel::INFO,
            "metrics " + toJsonLine(gateway.metrics()));
    }
}

int main()
{
    Logger::init();

    Logger::log(
        LogLevel::INFO,
        "Engine started");

    runThroughputPass();

    runLatencyPass("Latency");

    Logger::log(
        LogLevel::INFO,
        "Engine shutdown");

    Logger::shutdown();

    return 0;
}
