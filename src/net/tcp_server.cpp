#include "net/tcp_server.h"

#include "api/protocol.h"
#include "utils/logger.h"

#include <cstring>

#include "socket_compat.h"

using netcompat::RawSocket;
using netcompat::kInvalidSocket;
using netcompat::closeSocket;
using netcompat::ensureNetworkStack;
using netcompat::recvBytes;
using netcompat::sendAll;
using netcompat::shutdownBoth;

TcpServer::TcpServer(
    OrderGateway &gateway)
    : gateway(gateway),
      listener(static_cast<SocketHandle>(kInvalidSocket))
{
}

TcpServer::~TcpServer()
{
    stop();
}

bool TcpServer::start(
    unsigned short port)
{
    if (running)
        return false;

    ensureNetworkStack();

    RawSocket sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock == kInvalidSocket)
        return false;

    // Without SO_REUSEADDR a server that has just been stopped leaves
    // the port in TIME_WAIT and the next bind fails — which in a test
    // suite means the second test to use a port fails for reasons that
    // have nothing to do with the code under test.
    int reuse = 1;
    ::setsockopt(
        sock,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char *>(&reuse),
        sizeof(reuse));

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        closeSocket(sock);
        return false;
    }

    if (::listen(sock, 16) != 0)
    {
        closeSocket(sock);
        return false;
    }

    // Read back what was actually bound, which is the only way to learn
    // the port when 0 was requested.
    sockaddr_in actual;
    std::memset(&actual, 0, sizeof(actual));
    netcompat::AddressLength actual_length = sizeof(actual);

    if (::getsockname(sock, reinterpret_cast<sockaddr *>(&actual), &actual_length) == 0)
        bound_port = ntohs(actual.sin_port);
    else
        bound_port = port;

    listener = static_cast<SocketHandle>(sock);
    running = true;

    accept_thread = std::thread(&TcpServer::acceptLoop, this);

    Logger::log(
        LogLevel::INFO,
        "TCP server listening on 127.0.0.1:" + std::to_string(bound_port));

    return true;
}

unsigned short TcpServer::port() const
{
    return bound_port;
}

bool TcpServer::isRunning() const
{
    return running;
}

void TcpServer::acceptLoop()
{
    while (running)
    {
        RawSocket client = ::accept(
            static_cast<RawSocket>(listener),
            nullptr,
            nullptr);

        if (client == kInvalidSocket)
        {
            // Either the listener was closed by stop(), which is the
            // normal way out of this loop, or the accept genuinely
            // failed. Either way there is nothing useful to do but
            // check whether we are still meant to be running.
            if (!running)
                break;

            continue;
        }

        std::lock_guard<std::mutex>
            lock(connections_mutex);

        if (!running)
        {
            closeSocket(client);
            break;
        }

        connections.push_back(static_cast<SocketHandle>(client));

        connection_threads.emplace_back(
            &TcpServer::serveConnection,
            this,
            static_cast<SocketHandle>(client));
    }
}

void TcpServer::serveConnection(
    SocketHandle handle)
{
    const RawSocket client = static_cast<RawSocket>(handle);

    std::string pending;
    char buffer[4096];

    while (running)
    {
        const int received = recvBytes(client, buffer, static_cast<int>(sizeof(buffer)));

        if (received <= 0)
            break;

        pending.append(buffer, static_cast<std::size_t>(received));

        // TCP is a byte stream, not a message stream: one recv may hold
        // several requests, half a request, or both. Draining whole
        // lines out of an accumulating buffer is what turns the stream
        // back into discrete commands — reading "one recv, one request"
        // would work in every hand test and fail under a fast client.
        std::size_t newline = pending.find('\n');

        bool closing = false;

        while (newline != std::string::npos)
        {
            const std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);

            const ProtocolReply reply = handleCommand(gateway, line);

            if (!sendAll(client, reply.line + "\n"))
            {
                closing = true;
                break;
            }

            if (reply.close_connection)
            {
                closing = true;
                break;
            }

            newline = pending.find('\n');
        }

        if (closing)
            break;
    }

    closeSocket(client);

    std::lock_guard<std::mutex>
        lock(connections_mutex);

    for (std::size_t i = 0; i < connections.size(); ++i)
    {
        if (connections[i] == handle)
        {
            connections.erase(connections.begin() + static_cast<long>(i));
            break;
        }
    }
}

void TcpServer::stop()
{
    if (!running.exchange(false))
        return;

    // Closing the listening socket is what unblocks the accept thread;
    // it has no flag to notice while it is parked in accept().
    if (static_cast<RawSocket>(listener) != kInvalidSocket)
    {
        closeSocket(static_cast<RawSocket>(listener));
        listener = static_cast<SocketHandle>(kInvalidSocket);
    }

    if (accept_thread.joinable())
        accept_thread.join();

    // Same problem one level down: a connection thread parked in recv()
    // will not see running == false until its socket goes away.
    {
        std::lock_guard<std::mutex>
            lock(connections_mutex);

        for (SocketHandle handle : connections)
        {
            shutdownBoth(static_cast<RawSocket>(handle));
        }
    }

    // Moved out under the lock before joining: a finishing connection
    // thread takes connections_mutex on its way out, so joining while
    // holding it would deadlock.
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex>
            lock(connections_mutex);

        threads.swap(connection_threads);
    }

    for (std::thread &thread : threads)
    {
        if (thread.joinable())
            thread.join();
    }

    bound_port = 0;
}
