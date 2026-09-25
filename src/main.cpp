#include "api/order_gateway.h"
#include "net/tcp_server.h"
#include "utils/timer.h"
#include "utils/logger.h"
#include "utils/latency_stats.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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
    //  ran this twice, with logging live and suppressed, to isolate
    // how much of the tail belonged to the synchronous log write inside
    // the matching loop.next removed that write entirely — the two
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

    std::atomic<bool> stop_requested{false};

    void handleInterrupt(int)
    {
        // A signal handler may touch almost nothing safely. Setting one
        // atomic flag and letting the main thread do the real shutdown
        // is the standard shape, and keeps server.stop() — which joins
        // threads — out of the handler entirely.
        stop_requested = true;
    }

    bool stdinIsInteractive()
    {
#ifdef _WIN32
        return _isatty(_fileno(stdin)) != 0;
#else
        return isatty(fileno(stdin)) != 0;
#endif
    }

    //  The engine stays in-process and
    // single-owner; the server is just another caller of OrderGateway,
    // which is the point of having kept the gateway as the one entry
    // point all along.
    void runServer(
        unsigned short port)
    {
        OrderGateway gateway;

        TcpServer server(gateway);

        if (!server.start(port))
        {
            Logger::log(
                LogLevel::ERROR,
                "Could not bind port " + std::to_string(port));

            return;
        }

        std::cout
            << "Listening on 127.0.0.1:" << server.port() << '\n'
            << "Try: PING | SUBMIT 1 BUY LIMIT 100.0 10 | SNAPSHOT 5 | METRICS" << '\n'
            << (stdinIsInteractive()
                    ? "Press Enter or Ctrl-C to stop."
                    : "Send Ctrl-C to stop.")
            << std::endl;

        // How to wait for shutdown depends on how the process was
        // started, and getting this wrong makes the server undeployable
        // rather than merely awkward: simply blocking on stdin means a
        // process launched in the background, from a script, or with its
        // input redirected reads EOF immediately and exits on the spot.
        //
        // So Ctrl-C is always a way out, and Enter is offered as well
        // only when there is a terminal on the other end to press it.
        std::signal(SIGINT, handleInterrupt);

        if (stdinIsInteractive())
        {
            // Detached because a thread parked in getline cannot be
            // joined once SIGINT has already ended the wait. The
            // process is on its way out either way.
            std::thread(
                []()
                {
                    std::string ignored;
                    std::getline(std::cin, ignored);
                    stop_requested = true;
                })
                .detach();
        }

        while (!stop_requested)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        server.stop();

        Logger::log(
            LogLevel::INFO,
            "metrics " + toJsonLine(gateway.metrics()));
    }
}

int main(
    int argc,
    char **argv)
{
    Logger::init();

    Logger::log(
        LogLevel::INFO,
        "Engine started");

    // No arguments keeps the historical behaviour — the benchmark every
    // earlier day measured — so nothing that depended on running
    // engine.exe bare has changed.
    const std::string mode = (argc > 1) ? argv[1] : "";

    if (mode == "--serve")
    {
        const unsigned short port =
            (argc > 2)
                ? static_cast<unsigned short>(std::atoi(argv[2]))
                : 9001;

        runServer(port);
    }
    else
    {
        runThroughputPass();

        runLatencyPass("Latency");
    }

    Logger::log(
        LogLevel::INFO,
        "Engine shutdown");

    Logger::shutdown();

    return 0;
}
