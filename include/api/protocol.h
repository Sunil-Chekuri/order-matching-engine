#pragma once

#include <string>

#include "api/order_gateway.h"

// The wire protocol, deliberately separated from the transport.
//
// Everything that can be wrong about a request — unknown verbs, missing
// or extra fields, unparseable numbers, values the engine rejects — is
// decided here, by a function that takes a string and returns a string
// and touches no socket at all. That is the whole point of the split:
// a network server is awkward to test and slow to test, whereas this is
// an ordinary pure-ish function over a gateway, and it is where every
// interesting bug actually lives. tcp_server.cpp is then thin enough to
// be checked by a handful of round-trip tests.
//
// The protocol is line-based ASCII, one request per line, exactly one
// response line per request. That is not the fastest framing available
// — a binary length-prefixed format would avoid the parse entirely —
// but it can be driven by hand from netcat or telnet, which matters far
// more for a project meant to be demonstrated than the microseconds do.
//
// Requests (fields are whitespace-separated; [] marks optional):
//
//   PING
//   SUBMIT <id> <BUY|SELL> <LIMIT|MARKET|IOC|FOK> <price> <qty> [symbol] [participant]
//   CANCEL <id> [symbol]
//   QTY <id> [symbol]
//   SNAPSHOT <depth> [symbol]
//   METRICS
//   QUIT
//
// Responses are one line, beginning either "OK" or "ERR <reason>".
// Reasons are fixed lowercase tokens rather than prose, so a client can
// branch on them without parsing English.
struct ProtocolReply
{
    std::string line;

    // QUIT is the only request that asks the server to hang up. Making
    // that an explicit part of the reply keeps the decision in the
    // protocol layer, where it can be tested, instead of leaving the
    // transport to special-case the verb.
    bool close_connection = false;
};

ProtocolReply handleCommand(
    OrderGateway &gateway,
    const std::string &line);
