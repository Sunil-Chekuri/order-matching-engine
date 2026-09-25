#include <gtest/gtest.h>

#include "api/protocol.h"

#include <string>

namespace
{
    // The protocol layer takes a string and returns a string, so these
    // need no socket, no port and no timing — which is the entire
    // reason the transport was split away from it.
    std::string reply(
        OrderGateway &gateway,
        const std::string &line)
    {
        return handleCommand(gateway, line).line;
    }

    bool startsWith(
        const std::string &value,
        const std::string &prefix)
    {
        return value.rfind(prefix, 0) == 0;
    }
}

TEST(ProtocolTest, PingReturnsPong)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "PING"), "OK pong");
}

TEST(ProtocolTest, VerbsAreCaseInsensitive)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "ping"), "OK pong");
    EXPECT_EQ(reply(gateway, "PiNg"), "OK pong");
}

TEST(ProtocolTest, EmptyAndWhitespaceOnlyLinesAreRejected)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, ""), "ERR empty");
    EXPECT_EQ(reply(gateway, "   "), "ERR empty");
    EXPECT_EQ(reply(gateway, "\t"), "ERR empty");
}

TEST(ProtocolTest, UnknownVerbIsRejected)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "TRADE 1"), "ERR unknown_command");
}

TEST(ProtocolTest, TrailingCarriageReturnIsTolerated)
{
    // A CRLF client is the common case (telnet, most Windows clients).
    // If the \r survived tokenising, "DEFAULT\r" would become a second,
    // separate book and every later request would look at the wrong one.
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "PING\r"), "OK pong");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10\r"),
              "OK submitted 1 resting 10");

    // Proof it landed on the default book rather than one named with a
    // stray control character.
    EXPECT_EQ(reply(gateway, "QTY 1"), "OK qty 10");
}

TEST(ProtocolTest, SubmitRestsAnUnmatchedOrder)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10"),
              "OK submitted 1 resting 10");
}

TEST(ProtocolTest, SubmitReportsZeroRestingWhenFullyFilled)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10");

    EXPECT_EQ(reply(gateway, "SUBMIT 2 SELL LIMIT 100.0 10"),
              "OK submitted 2 resting 0");
}

TEST(ProtocolTest, SubmitReportsThePartialRemainderThatRests)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 4");

    // 10 offered against 4 of demand: 4 trade, 6 rest.
    EXPECT_EQ(reply(gateway, "SUBMIT 2 SELL LIMIT 100.0 10"),
              "OK submitted 2 resting 6");
}

TEST(ProtocolTest, MarketOrderRemainderIsDiscardedNotRested)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 SELL LIMIT 100.0 3");

    // A MARKET order sweeps what liquidity exists and discards the rest,
    // so nothing should be left resting even though 10 were requested.
    EXPECT_EQ(reply(gateway, "SUBMIT 2 BUY MARKET 1.0 10"),
              "OK submitted 2 resting 0");
}

TEST(ProtocolTest, SubmitAcceptsSymbolAndParticipant)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10 ACME 7"),
              "OK submitted 1 resting 10");

    // On ACME, not on the default book.
    EXPECT_EQ(reply(gateway, "QTY 1 ACME"), "OK qty 10");
    EXPECT_EQ(reply(gateway, "QTY 1"), "ERR not_found");
}

TEST(ProtocolTest, SubmitRejectsMalformedFields)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "SUBMIT"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10 SYM 7 extra"),
              "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "SUBMIT x BUY LIMIT 100.0 10"), "ERR bad_id");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 SIDEWAYS LIMIT 100.0 10"), "ERR bad_side");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY STOP 100.0 10"), "ERR bad_type");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT abc 10"), "ERR bad_price");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 abc"), "ERR bad_quantity");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10 SYM zz"),
              "ERR bad_participant");
}

TEST(ProtocolTest, PartiallyNumericFieldsAreRejectedRatherThanTruncated)
{
    // std::stoi("12abc") is 12 and std::stod stops at the first bad
    // character, so without a full-consumption check these would be
    // silently accepted as a different order than the client sent.
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "SUBMIT 12abc BUY LIMIT 100.0 10"), "ERR bad_id");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10x"), "ERR bad_quantity");
    EXPECT_EQ(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0xyz 10"), "ERR bad_price");
}

TEST(ProtocolTest, EngineValidationIsSurfacedAsAnErrorNotAnException)
{
    // Order's constructor throws std::invalid_argument on a non-positive
    // price or quantity (Day 2). If that escaped the protocol layer it
    // would unwind into the connection thread and drop a client for what
    // is only a bad request.
    OrderGateway gateway;

    EXPECT_TRUE(startsWith(reply(gateway, "SUBMIT 1 BUY LIMIT 0 10"), "ERR rejected"));
    EXPECT_TRUE(startsWith(reply(gateway, "SUBMIT 1 BUY LIMIT -5 10"), "ERR rejected"));
    EXPECT_TRUE(startsWith(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 0"), "ERR rejected"));
    EXPECT_TRUE(startsWith(reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 -3"), "ERR rejected"));

    // And the gateway is still usable afterwards.
    EXPECT_EQ(reply(gateway, "PING"), "OK pong");
}

TEST(ProtocolTest, CancelRemovesARestingOrder)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10");

    EXPECT_EQ(reply(gateway, "CANCEL 1"), "OK cancelled 1");
    EXPECT_EQ(reply(gateway, "QTY 1"), "ERR not_found");
}

TEST(ProtocolTest, CancelOfAnAbsentOrderIsNotFound)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "CANCEL 99"), "ERR not_found");
}

TEST(ProtocolTest, CancelIsScopedToItsSymbol)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10 ACME");

    // Right id, wrong book.
    EXPECT_EQ(reply(gateway, "CANCEL 1"), "ERR not_found");
    EXPECT_EQ(reply(gateway, "CANCEL 1 ACME"), "OK cancelled 1");
}

TEST(ProtocolTest, CancelRejectsMalformedFields)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "CANCEL"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "CANCEL 1 SYM extra"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "CANCEL abc"), "ERR bad_id");
}

TEST(ProtocolTest, QuantityReportsWhatIsStillResting)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10");
    reply(gateway, "SUBMIT 2 SELL LIMIT 100.0 4");

    EXPECT_EQ(reply(gateway, "QTY 1"), "OK qty 6");
}

TEST(ProtocolTest, QuantityRejectsMalformedFields)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "QTY"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "QTY abc"), "ERR bad_id");
}

TEST(ProtocolTest, SnapshotRendersBothSidesAsJson)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 99.0 5");
    reply(gateway, "SUBMIT 2 SELL LIMIT 101.0 7");

    EXPECT_EQ(
        reply(gateway, "SNAPSHOT 5"),
        "OK {\"bids\":[{\"price\":99,\"quantity\":5,\"orders\":1}],"
        "\"asks\":[{\"price\":101,\"quantity\":7,\"orders\":1}]}");
}

TEST(ProtocolTest, SnapshotOfAnUntouchedSymbolIsEmptyNotAnError)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "SNAPSHOT 5 NOSUCH"),
              "OK {\"bids\":[],\"asks\":[]}");
}

TEST(ProtocolTest, SnapshotRespectsRequestedDepth)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 99.0 5");
    reply(gateway, "SUBMIT 2 BUY LIMIT 98.0 5");
    reply(gateway, "SUBMIT 3 BUY LIMIT 97.0 5");

    // Best price first, and only the one level asked for.
    EXPECT_EQ(reply(gateway, "SNAPSHOT 1"),
              "OK {\"bids\":[{\"price\":99,\"quantity\":5,\"orders\":1}],\"asks\":[]}");
}

TEST(ProtocolTest, SnapshotRejectsMalformedOrNegativeDepth)
{
    // A negative depth converts to an enormous size_t, which would ask
    // the book to walk every level it has.
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "SNAPSHOT"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "SNAPSHOT abc"), "ERR bad_depth");
    EXPECT_EQ(reply(gateway, "SNAPSHOT -1"), "ERR bad_depth");
}

TEST(ProtocolTest, SnapshotPriceSurvivesTheRoundTripThroughText)
{
    // What matters is not how the price is spelled but that parsing the
    // rendering back yields the identical double. Asserting on the
    // literal text would be wrong: 100.1234567890123 is not exactly
    // representable, and the nearest double prints as
    // 100.12345678901229 at full precision -- which is correct, and
    // round-trips exactly.
    const std::string requested = "100.1234567890123";

    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT " + requested + " 5");

    const std::string line = reply(gateway, "SNAPSHOT 1");

    const std::string key = "\"price\":";
    const std::size_t start = line.find(key);
    ASSERT_NE(start, std::string::npos) << line;

    const std::size_t from = start + key.size();
    const std::size_t end = line.find(',', from);
    ASSERT_NE(end, std::string::npos) << line;

    const double rendered = std::stod(line.substr(from, end - from));

    EXPECT_EQ(rendered, std::stod(requested)) << line;

    // And the guard is doing real work: the six-decimal rendering
    // std::to_string produces would land on a different price level.
    EXPECT_NE(std::stod(std::to_string(std::stod(requested))),
              std::stod(requested));
}

TEST(ProtocolTest, MetricsReportsEngineCounters)
{
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10");
    reply(gateway, "SUBMIT 2 SELL LIMIT 100.0 10");

    EXPECT_EQ(
        reply(gateway, "METRICS"),
        "OK {\"orders_submitted\":2,\"trades_executed\":1,"
        "\"cancels_accepted\":0,\"cancels_rejected\":0,"
        "\"resting_orders\":0,\"symbols\":1}");
}

TEST(ProtocolTest, MetricsAndPingRejectExtraArguments)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "METRICS now"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "PING me"), "ERR bad_arity");
    EXPECT_EQ(reply(gateway, "QUIT now"), "ERR bad_arity");
}

TEST(ProtocolTest, QuitAsksTheTransportToCloseAndNothingElseDoes)
{
    OrderGateway gateway;

    const ProtocolReply quit = handleCommand(gateway, "QUIT");
    EXPECT_EQ(quit.line, "OK bye");
    EXPECT_TRUE(quit.close_connection);

    EXPECT_FALSE(handleCommand(gateway, "PING").close_connection);
    EXPECT_FALSE(handleCommand(gateway, "NONSENSE").close_connection);
    EXPECT_FALSE(handleCommand(gateway, "").close_connection);
}

TEST(ProtocolTest, ExtraWhitespaceBetweenFieldsIsAccepted)
{
    OrderGateway gateway;

    EXPECT_EQ(reply(gateway, "  SUBMIT   1   BUY   LIMIT   100.0   10  "),
              "OK submitted 1 resting 10");
}

TEST(ProtocolTest, SelfTradePreventionIsReachableFromTheWire)
{
    // Same participant on both sides: the resting order must not trade
    // with its owner, so the incoming order rests instead of filling.
    OrderGateway gateway;

    reply(gateway, "SUBMIT 1 BUY LIMIT 100.0 10 ACME 42");

    EXPECT_EQ(reply(gateway, "SUBMIT 2 SELL LIMIT 100.0 10 ACME 42"),
              "OK submitted 2 resting 10");
}
