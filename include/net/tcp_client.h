#pragma once

#include <string>

// A minimal blocking client for the line protocol.
//
// It exists for two reasons, neither of them speculative: the server's
// round-trip tests need something to talk to it with, and Day 16's depth
// display needs to read snapshots over the wire. Writing it once here is
// cheaper than duplicating a socket shim inside a test file, and it
// keeps <winsock2.h> out of the public headers.
//
// Blocking and synchronous on purpose — request, then response, one at
// a time. That matches the protocol (exactly one reply line per request)
// and is all either caller needs; anything pipelined would be a
// different class.
class TcpClient
{
public:
    TcpClient();

    ~TcpClient();

    TcpClient(const TcpClient &) = delete;
    TcpClient &operator=(const TcpClient &) = delete;

    bool connectTo(
        const std::string &host,
        unsigned short port);

    bool isConnected() const;

    // Appends the newline the protocol frames on, so callers pass the
    // command rather than remembering the terminator.
    bool sendLine(
        const std::string &line);

    // Sends exactly the bytes given, adding nothing. Needed to write a
    // request in pieces, or several in one write, which is how the
    // server ends up with a partial or a batched line to reframe.
    bool sendRaw(
        const std::string &bytes);

    // Reads one reply line, minus its terminator. False if the peer
    // closed or the connection failed before a full line arrived.
    bool readLine(
        std::string &out);

    // Convenience for the overwhelmingly common request/response pair.
    bool request(
        const std::string &line,
        std::string &out);

    void close();

private:
#ifdef _WIN32
    using SocketHandle = unsigned long long;
#else
    using SocketHandle = int;
#endif

    SocketHandle socket_handle;

    // Bytes read past the end of the current line. TCP does not
    // preserve message boundaries, so a read can return the tail of one
    // reply and the head of the next; dropping the remainder would
    // desynchronise every later call.
    std::string pending;
};
