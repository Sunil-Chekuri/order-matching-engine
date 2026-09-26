// Live order book depth, read over the Day 15 protocol.
//
// Deliberately a separate executable from engine.exe rather than another
// mode on it: this is a client. It holds no engine, and the only thing
// it can do to the book is look at it. Keeping that split visible in the
// build is worth more than saving a target.
//
// Everything interesting — parsing the replies, laying out the ladder —
// lives in src/cli/depth_view.cpp and is unit tested. What is left here
// is a connect, a loop, and a screen clear.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "cli/depth_view.h"
#include "net/tcp_client.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
    std::atomic<bool> stop_requested{false};

    void handleInterrupt(int)
    {
        stop_requested = true;
    }

    // Windows consoles do not interpret ANSI escapes unless asked to,
    // and have only done so at all since Windows 10. If this fails the
    // display still works — frames scroll instead of repainting in
    // place — so the return value is deliberately not fatal.
    void enableAnsiEscapes()
    {
#ifdef _WIN32
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

        if (out == INVALID_HANDLE_VALUE)
            return;

        DWORD mode = 0;

        if (!GetConsoleMode(out, &mode))
            return;

        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    }

    // Home the cursor and clear forward, rather than clearing the whole
    // screen first: clearing then drawing leaves a visible blank frame
    // between repaints, which reads as a flicker at any useful refresh
    // rate.
    void repaint(
        const std::string &frame)
    {
        std::cout << "\x1b[H\x1b[J" << frame << std::flush;
    }

    void usage()
    {
        std::cout
            << "usage: book_view [host] [port] [symbol] [depth] [interval_ms]\n"
            << "defaults: 127.0.0.1 9001 DEFAULT 10 500\n"
            << "start the server first with: engine.exe --serve 9001\n";
    }
}

int main(
    int argc,
    char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--help")
    {
        usage();
        return 0;
    }

    const std::string host = (argc > 1) ? argv[1] : "127.0.0.1";

    const unsigned short port =
        (argc > 2) ? static_cast<unsigned short>(std::atoi(argv[2])) : 9001;

    const std::string symbol = (argc > 3) ? argv[3] : "DEFAULT";

    const int depth = (argc > 4) ? std::atoi(argv[4]) : 10;

    const int interval_ms = (argc > 5) ? std::atoi(argv[5]) : 500;

    TcpClient client;

    if (!client.connectTo(host, port))
    {
        std::cout
            << "could not connect to " << host << ":" << port << "\n"
            << "is the server running? engine.exe --serve " << port << "\n";

        return 1;
    }

    std::signal(SIGINT, handleInterrupt);

    enableAnsiEscapes();

    const std::string snapshot_command =
        "SNAPSHOT " + std::to_string(depth) + " " + symbol;

    while (!stop_requested)
    {
        std::string snapshot_reply;
        std::string metrics_reply;

        // Two round trips per frame. The alternative — one verb
        // returning both — would couple depth and counters together for
        // the convenience of this one client, and the measurements on
        // Day 15 put a round trip at roughly 23 microseconds against a
        // refresh interval measured in hundreds of milliseconds.
        if (!client.request(snapshot_command, snapshot_reply) ||
            !client.request("METRICS", metrics_reply))
        {
            std::cout << "\nconnection lost\n";
            return 1;
        }

        BookSnapshot snapshot;
        EngineMetrics metrics;
        std::string error;

        if (!parseSnapshotReply(snapshot_reply, snapshot, error))
        {
            std::cout << "\nbad snapshot reply: " << error << "\n";
            return 1;
        }

        if (!parseMetricsReply(metrics_reply, metrics, error))
        {
            std::cout << "\nbad metrics reply: " << error << "\n";
            return 1;
        }

        repaint(
            renderDepth(symbol, snapshot) +
            "\n" + renderMetrics(metrics) +
            "\n\nrefreshing every " + std::to_string(interval_ms) +
            " ms - Ctrl-C to stop\n");

        // Slept in slices so Ctrl-C is honoured promptly even when the
        // refresh interval is long.
        for (int slept = 0; slept < interval_ms && !stop_requested; slept += 50)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    interval_ms - slept < 50 ? interval_ms - slept : 50));
        }
    }

    std::cout << "\nstopped\n";

    return 0;
}
