#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <string>

#include "core/engine_metrics.h"
#include "core/order.h"
#include "engine/matching_engine.h"
#include "utils/logger.h"

TEST(MetricsTest, AFreshEngineReportsNothing)
{
    MatchingEngine engine;

    EngineMetrics metrics = engine.metrics();

    EXPECT_EQ(metrics.orders_submitted, 0);
    EXPECT_EQ(metrics.trades_executed, 0);
    EXPECT_EQ(metrics.cancels_accepted, 0);
    EXPECT_EQ(metrics.cancels_rejected, 0);
    EXPECT_EQ(metrics.resting_orders, 0);
    EXPECT_EQ(metrics.symbols, 0u);
}

TEST(MetricsTest, SubmissionsAreCounted)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 99.0, 10, Side::BUY));

    EXPECT_EQ(engine.metrics().orders_submitted, 2);
}

TEST(MetricsTest, RestingOrdersIsAGaugeNotACounter)
{
    // It must fall when orders leave the book, unlike the cumulative
    // counters beside it.
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 99.0, 10, Side::BUY));

    EXPECT_EQ(engine.metrics().resting_orders, 2);

    engine.processOrder(Order(3, 100.0, 10, Side::SELL));

    EngineMetrics after = engine.metrics();
    EXPECT_EQ(after.resting_orders, 1);
    EXPECT_EQ(after.orders_submitted, 3);
    EXPECT_EQ(after.trades_executed, 1);
}

TEST(MetricsTest, CancelsAreSplitByOutcome)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_TRUE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(1));
    EXPECT_FALSE(engine.cancelOrder(999));

    EngineMetrics metrics = engine.metrics();
    EXPECT_EQ(metrics.cancels_accepted, 1);
    EXPECT_EQ(metrics.cancels_rejected, 2);
}

TEST(MetricsTest, CancelsForAnUnknownSymbolAreNotCountedAgainstAnyShard)
{
    // There is no shard to attribute them to, and creating one just to
    // record a failure would leak a book per bad request.
    MatchingEngine engine;

    EXPECT_FALSE(engine.cancelOrder(1, "NOSUCH"));

    EXPECT_EQ(engine.metrics().symbols, 0u);
    EXPECT_EQ(engine.metrics().cancels_rejected, 0);
}

TEST(MetricsTest, TotalsAggregateAcrossSymbols)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL, OrderType::LIMIT, 0, "AAPL"));
    engine.processOrder(Order(3, 50.0, 5, Side::BUY, OrderType::LIMIT, 0, "MSFT"));

    EngineMetrics metrics = engine.metrics();
    EXPECT_EQ(metrics.symbols, 2u);
    EXPECT_EQ(metrics.orders_submitted, 3);
    EXPECT_EQ(metrics.trades_executed, 1);
    EXPECT_EQ(metrics.resting_orders, 1);
}

TEST(MetricsTest, TradeCountAgreesWithGetTotalTrades)
{
    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 100.0, 10, Side::SELL));

    EXPECT_EQ(engine.metrics().trades_executed, engine.getTotalTrades());
}

TEST(MetricsTest, JsonRenderingContainsEveryField)
{
    EngineMetrics metrics;
    metrics.orders_submitted = 12;
    metrics.trades_executed = 3;
    metrics.cancels_accepted = 2;
    metrics.cancels_rejected = 1;
    metrics.resting_orders = 7;
    metrics.symbols = 4;

    std::string json = toJsonLine(metrics);

    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
    EXPECT_NE(json.find("\"orders_submitted\":12"), std::string::npos);
    EXPECT_NE(json.find("\"trades_executed\":3"), std::string::npos);
    EXPECT_NE(json.find("\"cancels_accepted\":2"), std::string::npos);
    EXPECT_NE(json.find("\"cancels_rejected\":1"), std::string::npos);
    EXPECT_NE(json.find("\"resting_orders\":7"), std::string::npos);
    EXPECT_NE(json.find("\"symbols\":4"), std::string::npos);
    EXPECT_EQ(json.find('\n'), std::string::npos);
}

// ---- Log level filtering ----

namespace
{
    std::string captureLog(LogLevel level, const std::string &message)
    {
        std::ostringstream captured;
        std::streambuf *original = std::cout.rdbuf(captured.rdbuf());

        Logger::log(level, message);

        std::cout.rdbuf(original);

        return captured.str();
    }
}

TEST(MetricsTest, DebugMessagesAreDiscardedByDefault)
{
    Logger::setEnabled(true);
    Logger::setMinLevel(LogLevel::INFO);

    EXPECT_TRUE(captureLog(LogLevel::DEBUG, "chatter").empty());
    EXPECT_NE(captureLog(LogLevel::INFO, "notable").find("notable"),
              std::string::npos);
}

TEST(MetricsTest, LoweringTheThresholdLetsDebugThrough)
{
    Logger::setEnabled(true);
    Logger::setMinLevel(LogLevel::DEBUG);

    EXPECT_NE(captureLog(LogLevel::DEBUG, "chatter").find("chatter"),
              std::string::npos);

    Logger::setMinLevel(LogLevel::INFO);
}

TEST(MetricsTest, RaisingTheThresholdSuppressesLesserSeverities)
{
    Logger::setEnabled(true);
    Logger::setMinLevel(LogLevel::ERROR);

    EXPECT_TRUE(captureLog(LogLevel::INFO, "info").empty());
    EXPECT_TRUE(captureLog(LogLevel::WARNING, "warning").empty());
    EXPECT_NE(captureLog(LogLevel::ERROR, "error").find("error"),
              std::string::npos);

    Logger::setMinLevel(LogLevel::INFO);
}

TEST(MetricsTest, CancellingDoesNotWriteAnythingAtDefaultVerbosity)
{
    // The regression this day exists to prevent: cancels used to log at
    // INFO, so every one did a formatted write and a flush on the hot
    // path.
    Logger::setEnabled(true);
    Logger::setMinLevel(LogLevel::INFO);

    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));

    std::ostringstream captured;
    std::streambuf *original = std::cout.rdbuf(captured.rdbuf());

    engine.cancelOrder(1);
    engine.cancelOrder(1);

    std::cout.rdbuf(original);

    EXPECT_TRUE(captured.str().empty());
}
