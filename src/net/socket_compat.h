#pragma once

// Private to src/net — deliberately not in include/, because nothing
// outside the transport should be pulling in <winsock2.h>.
//
// Every difference between Winsock and BSD sockets is confined here:
// the handle type, the invalid value, closing, and the two send/recv
// quirks that differ. Both tcp_server.cpp and tcp_client.cpp include
// this, so the platform code exists once rather than once per file.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <string>

namespace netcompat
{
#ifdef _WIN32
    using RawSocket = SOCKET;

    inline const RawSocket kInvalidSocket = INVALID_SOCKET;

    using AddressLength = int;

    inline void closeSocket(RawSocket s)
    {
        ::closesocket(s);
    }

    inline void shutdownBoth(RawSocket s)
    {
        ::shutdown(s, SD_BOTH);
    }

    // Winsock must be initialised per process before any socket call and
    // torn down after the last one. A function-local static gives that
    // with thread-safe initialisation, and without any caller needing to
    // know Windows exists.
    struct WinsockGuard
    {
        WinsockGuard()
        {
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
        }

        ~WinsockGuard()
        {
            WSACleanup();
        }
    };

    inline void ensureNetworkStack()
    {
        static WinsockGuard guard;
    }

    inline int recvBytes(RawSocket s, char *buffer, int length)
    {
        return ::recv(s, buffer, length, 0);
    }

    inline int sendBytes(RawSocket s, const char *buffer, int length)
    {
        return ::send(s, buffer, length, 0);
    }
#else
    using RawSocket = int;

    inline const RawSocket kInvalidSocket = -1;

    using AddressLength = socklen_t;

    inline void closeSocket(RawSocket s)
    {
        ::close(s);
    }

    inline void shutdownBoth(RawSocket s)
    {
        ::shutdown(s, SHUT_RDWR);
    }

    inline void ensureNetworkStack()
    {
    }

    inline int recvBytes(RawSocket s, char *buffer, int length)
    {
        return static_cast<int>(
            ::recv(s, buffer, static_cast<std::size_t>(length), 0));
    }

    inline int sendBytes(RawSocket s, const char *buffer, int length)
    {
        // MSG_NOSIGNAL stops a write to a socket the peer already closed
        // from killing the whole process with SIGPIPE — the POSIX
        // default, and a difference from Windows that would otherwise
        // only surface once CI runs on Linux.
        return static_cast<int>(
            ::send(s, buffer, static_cast<std::size_t>(length), MSG_NOSIGNAL));
    }
#endif

    inline bool sendAll(
        RawSocket socket,
        const std::string &payload)
    {
        std::size_t sent = 0;

        while (sent < payload.size())
        {
            const int wrote = sendBytes(
                socket,
                payload.data() + sent,
                static_cast<int>(payload.size() - sent));

            if (wrote <= 0)
                return false;

            sent += static_cast<std::size_t>(wrote);
        }

        return true;
    }
}
