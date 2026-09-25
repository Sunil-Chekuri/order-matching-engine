#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api/order_gateway.h"

// A deliberately thin TCP front end for OrderGateway.
//
// Thin is the design goal, not an apology: everything that can be got
// wrong about a request is decided in protocol.cpp, which needs no
// socket to test. What is left here is accept, read lines, write the
// reply back — the part that genuinely needs a network to exercise, and
// therefore the part worth keeping small.
//
// Concurrency model: one thread per connection, sharing one gateway.
// That is only viable because of Week 2's work — the engine shards per
// symbol and holds a shard lock across each whole operation, so the
// server needs no lock of its own. It is also the model's limit: a
// thread per connection stops scaling in the thousands, where the
// answer is an event loop (epoll/IOCP) rather than more threads. At the
// scale this project demonstrates, threads are the honest choice and
// the bottleneck remains the shard lookup measured on Day 11.
//
// Protocol framing is a plain '\n'-terminated line. A client that sends
// CRLF is handled, because the tokeniser splits on whitespace.
class TcpServer
{
public:
    explicit TcpServer(
        OrderGateway &gateway);

    ~TcpServer();

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    // Binds, listens, and starts accepting on a background thread, so
    // the caller keeps control. Returns false if the port could not be
    // bound. Pass 0 to let the OS choose a free port and then read it
    // back with port() — which is what the tests do, so that running
    // them never collides with a port something else is already using.
    bool start(
        unsigned short port);

    // The port actually bound, which differs from the requested one
    // when 0 was requested. Zero before a successful start().
    unsigned short port() const;

    bool isRunning() const;

    // Stops accepting and closes every live connection, then joins all
    // threads. Safe to call twice, and called by the destructor, so a
    // server cannot outlive its threads.
    void stop();

private:
    // A native socket handle. Windows and POSIX disagree about both the
    // type and the invalid value, so the difference is confined to
    // these two aliases and the small helpers in the .cpp rather than
    // being spread through the logic.
#ifdef _WIN32
    using SocketHandle = unsigned long long;
#else
    using SocketHandle = int;
#endif

    void acceptLoop();

    void serveConnection(
        SocketHandle client);

    OrderGateway &gateway;

    SocketHandle listener;

    std::atomic<bool> running{false};

    unsigned short bound_port = 0;

    std::thread accept_thread;

    // Live client sockets, so stop() can shut them down: a connection
    // thread parked in recv() will not notice a flag, and would
    // otherwise keep the process alive until the client happened to
    // disconnect.
    mutable std::mutex connections_mutex;
    std::vector<SocketHandle> connections;
    std::vector<std::thread> connection_threads;
};
