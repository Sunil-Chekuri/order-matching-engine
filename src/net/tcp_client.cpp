#include "net/tcp_client.h"

#include <cstring>

#include "socket_compat.h"

using netcompat::RawSocket;
using netcompat::kInvalidSocket;
using netcompat::closeSocket;
using netcompat::ensureNetworkStack;
using netcompat::recvBytes;
using netcompat::sendAll;

TcpClient::TcpClient()
    : socket_handle(static_cast<SocketHandle>(kInvalidSocket))
{
}

TcpClient::~TcpClient()
{
    close();
}

bool TcpClient::connectTo(
    const std::string &host,
    unsigned short port)
{
    close();

    ensureNetworkStack();

    RawSocket sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock == kInvalidSocket)
        return false;

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // Numeric addresses only. Resolving names would mean getaddrinfo and
    // a second code path for very little: this client talks to a server
    // on loopback or a known host, never to a name that needs DNS.
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
    {
        closeSocket(sock);
        return false;
    }

    if (::connect(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        closeSocket(sock);
        return false;
    }

    socket_handle = static_cast<SocketHandle>(sock);
    pending.clear();

    return true;
}

bool TcpClient::isConnected() const
{
    return static_cast<RawSocket>(socket_handle) != kInvalidSocket;
}

bool TcpClient::sendLine(
    const std::string &line)
{
    if (!isConnected())
        return false;

    return sendAll(
        static_cast<RawSocket>(socket_handle),
        line + "\n");
}

bool TcpClient::sendRaw(
    const std::string &bytes)
{
    if (!isConnected())
        return false;

    return sendAll(
        static_cast<RawSocket>(socket_handle),
        bytes);
}

bool TcpClient::readLine(
    std::string &out)
{
    if (!isConnected())
        return false;

    char buffer[4096];

    while (true)
    {
        const std::size_t newline = pending.find('\n');

        if (newline != std::string::npos)
        {
            out = pending.substr(0, newline);
            pending.erase(0, newline + 1);

            // Tolerate a CRLF-terminated server even though this one
            // does not send it, so the client stays usable against
            // anything else speaking the same protocol.
            if (!out.empty() && out.back() == '\r')
                out.pop_back();

            return true;
        }

        const int received = recvBytes(
            static_cast<RawSocket>(socket_handle),
            buffer,
            static_cast<int>(sizeof(buffer)));

        if (received <= 0)
            return false;

        pending.append(buffer, static_cast<std::size_t>(received));
    }
}

bool TcpClient::request(
    const std::string &line,
    std::string &out)
{
    if (!sendLine(line))
        return false;

    return readLine(out);
}

void TcpClient::close()
{
    if (!isConnected())
        return;

    closeSocket(static_cast<RawSocket>(socket_handle));
    socket_handle = static_cast<SocketHandle>(kInvalidSocket);
    pending.clear();
}
