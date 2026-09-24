#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "core/order.h"
#include "engine/matching_engine.h"
#include "persistence/write_ahead_log.h"

namespace
{
    // Each test writes to its own file and removes it on both entry and
    // exit, so a crashed run cannot poison the next one.
    class WalFile
    {
    public:
        explicit WalFile(const std::string &name)
            : path("test_wal_" + name + ".log")
        {
            std::remove(path.c_str());
        }

        ~WalFile()
        {
            std::remove(path.c_str());
        }

        const std::string &get() const
        {
            return path;
        }

    private:
        std::string path;
    };

    void expectSameBook(
        const BookSnapshot &expected,
        const BookSnapshot &actual)
    {
        ASSERT_EQ(expected.bids.size(), actual.bids.size());
        ASSERT_EQ(expected.asks.size(), actual.asks.size());

        for (std::size_t i = 0; i < expected.bids.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(expected.bids[i].price, actual.bids[i].price);
            EXPECT_EQ(expected.bids[i].total_quantity, actual.bids[i].total_quantity);
            EXPECT_EQ(expected.bids[i].order_count, actual.bids[i].order_count);
        }

        for (std::size_t i = 0; i < expected.asks.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(expected.asks[i].price, actual.asks[i].price);
            EXPECT_EQ(expected.asks[i].total_quantity, actual.asks[i].total_quantity);
            EXPECT_EQ(expected.asks[i].order_count, actual.asks[i].order_count);
        }
    }
}

// ---- The log itself ----

TEST(WriteAheadLogTest, ReadingAMissingFileYieldsNothing)
{
    EXPECT_TRUE(WriteAheadLog::readAll("test_wal_does_not_exist.log").empty());
}

TEST(WriteAheadLogTest, SubmitRecordsRoundTrip)
{
    WalFile file("roundtrip");

    WriteAheadLog log;
    log.open(file.get());
    log.appendSubmit(Order(7, 101.25, 30, Side::SELL, OrderType::IOC, 42, "AAPL"));
    log.close();

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].kind, WalRecord::Kind::SUBMIT);
    EXPECT_EQ(records[0].order_id, 7);
    EXPECT_DOUBLE_EQ(records[0].price, 101.25);
    EXPECT_EQ(records[0].quantity, 30);
    EXPECT_EQ(records[0].side, Side::SELL);
    EXPECT_EQ(records[0].type, OrderType::IOC);
    EXPECT_EQ(records[0].participant_id, 42);
    EXPECT_EQ(records[0].symbol, "AAPL");
}

TEST(WriteAheadLogTest, PricesSurviveTheTextRoundTripExactly)
{
    // std::to_string would truncate this to six decimals and the replayed
    // book would sit at a different price level than the original.
    WalFile file("precision");

    const double awkward = 100.123456789012345;

    WriteAheadLog log;
    log.open(file.get());
    log.appendSubmit(Order(1, awkward, 10, Side::BUY));
    log.close();

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 1u);
    EXPECT_DOUBLE_EQ(records[0].price, awkward);
}

TEST(WriteAheadLogTest, CancelRecordsRoundTrip)
{
    WalFile file("cancel");

    WriteAheadLog log;
    log.open(file.get());
    log.appendCancel(99, "MSFT");
    log.close();

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].kind, WalRecord::Kind::CANCEL);
    EXPECT_EQ(records[0].order_id, 99);
    EXPECT_EQ(records[0].symbol, "MSFT");
}

TEST(WriteAheadLogTest, RecordsAreReadBackInWrittenOrder)
{
    WalFile file("order");

    WriteAheadLog log;
    log.open(file.get());
    for (int i = 1; i <= 50; ++i)
        log.appendSubmit(Order(i, 100.0, 10, Side::BUY));
    log.close();

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 50u);
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(records[i].order_id, i + 1);
}

TEST(WriteAheadLogTest, AppendingToAnUnopenedLogIsANoOp)
{
    WriteAheadLog log;

    EXPECT_FALSE(log.isOpen());
    log.appendSubmit(Order(1, 100.0, 10, Side::BUY));
    log.appendCancel(1, "AAPL");
}

TEST(WriteAheadLogTest, ATruncatedFinalLineIsSkippedAndEarlierRecordsSurvive)
{
    // What a crash mid-write actually leaves behind.
    WalFile file("truncated");

    {
        std::ofstream raw(file.get());
        raw << "S,1,100,10,0,0,0,AAPL\n";
        raw << "S,2,100,10,0,0,0,AAPL\n";
        raw << "S,3,100,1";
    }

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].order_id, 1);
    EXPECT_EQ(records[1].order_id, 2);
}

// ---- Engine integration ----

TEST(WriteAheadLogTest, EngineRecordsNothingUntilLoggingIsEnabled)
{
    WalFile file("disabled");

    MatchingEngine engine;
    engine.processOrder(Order(1, 100.0, 10, Side::BUY));

    EXPECT_TRUE(WriteAheadLog::readAll(file.get()).empty());
}

TEST(WriteAheadLogTest, EngineRecordsEveryAcceptedCommand)
{
    WalFile file("records");

    MatchingEngine engine;
    engine.enableWriteAheadLog(file.get());

    engine.processOrder(Order(1, 100.0, 10, Side::BUY));
    engine.processOrder(Order(2, 101.0, 5, Side::SELL));
    engine.cancelOrder(1);

    engine.disableWriteAheadLog();

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].kind, WalRecord::Kind::SUBMIT);
    EXPECT_EQ(records[1].kind, WalRecord::Kind::SUBMIT);
    EXPECT_EQ(records[2].kind, WalRecord::Kind::CANCEL);
    EXPECT_EQ(records[2].order_id, 1);
}

TEST(WriteAheadLogTest, ReplayRebuildsAnIdenticalRestingBook)
{
    WalFile file("replay_book");

    MatchingEngine original;
    original.enableWriteAheadLog(file.get());

    for (int i = 0; i < 20; ++i)
        original.processOrder(Order(i + 1, 100.0 - i, 10 + i, Side::BUY));

    for (int i = 0; i < 15; ++i)
        original.processOrder(Order(100 + i, 200.0 + i, 7, Side::SELL));

    original.disableWriteAheadLog();

    MatchingEngine recovered;
    EXPECT_EQ(recovered.replayFrom(file.get()), 35u);

    expectSameBook(original.snapshot(50), recovered.snapshot(50));
}

TEST(WriteAheadLogTest, ReplayReproducesTradesAndPartialFills)
{
    WalFile file("replay_trades");

    MatchingEngine original;
    original.enableWriteAheadLog(file.get());

    original.processOrder(Order(1, 100.0, 10, Side::BUY));
    original.processOrder(Order(2, 100.0, 4, Side::SELL));
    original.processOrder(Order(3, 100.0, 3, Side::SELL));

    original.disableWriteAheadLog();

    MatchingEngine recovered;
    recovered.replayFrom(file.get());

    EXPECT_EQ(recovered.getTotalTrades(), original.getTotalTrades());

    int original_remaining = -1;
    int recovered_remaining = -2;
    ASSERT_TRUE(original.getRemainingQuantity(1, original_remaining));
    ASSERT_TRUE(recovered.getRemainingQuantity(1, recovered_remaining));
    EXPECT_EQ(recovered_remaining, original_remaining);
    EXPECT_EQ(recovered_remaining, 3);
}

TEST(WriteAheadLogTest, ReplayHonoursCancellations)
{
    WalFile file("replay_cancel");

    MatchingEngine original;
    original.enableWriteAheadLog(file.get());

    original.processOrder(Order(1, 100.0, 10, Side::BUY));
    original.processOrder(Order(2, 99.0, 10, Side::BUY));
    original.cancelOrder(1);

    original.disableWriteAheadLog();

    MatchingEngine recovered;
    recovered.replayFrom(file.get());

    BookSnapshot snapshot = recovered.snapshot(10);
    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 99.0);
}

TEST(WriteAheadLogTest, ReplayRestoresEverySymbolSeparately)
{
    WalFile file("replay_symbols");

    MatchingEngine original;
    original.enableWriteAheadLog(file.get());

    original.processOrder(Order(1, 100.0, 10, Side::BUY, OrderType::LIMIT, 0, "AAPL"));
    original.processOrder(Order(2, 50.0, 5, Side::BUY, OrderType::LIMIT, 0, "MSFT"));
    original.processOrder(Order(3, 75.0, 8, Side::SELL, OrderType::LIMIT, 0, "TSLA"));

    original.disableWriteAheadLog();

    MatchingEngine recovered;
    recovered.replayFrom(file.get());

    EXPECT_EQ(recovered.symbolCount(), 3u);
    expectSameBook(original.snapshot(5, "AAPL"), recovered.snapshot(5, "AAPL"));
    expectSameBook(original.snapshot(5, "MSFT"), recovered.snapshot(5, "MSFT"));
    expectSameBook(original.snapshot(5, "TSLA"), recovered.snapshot(5, "TSLA"));
}

TEST(WriteAheadLogTest, ReplayDoesNotRewriteTheLogItReadFrom)
{
    // Otherwise every recovery would double the log, and the next one
    // would double it again.
    WalFile file("replay_no_echo");

    MatchingEngine original;
    original.enableWriteAheadLog(file.get());
    original.processOrder(Order(1, 100.0, 10, Side::BUY));
    original.processOrder(Order(2, 99.0, 10, Side::BUY));
    original.disableWriteAheadLog();

    MatchingEngine recovered;
    recovered.enableWriteAheadLog(file.get());
    recovered.replayFrom(file.get());
    recovered.disableWriteAheadLog();

    EXPECT_EQ(WriteAheadLog::readAll(file.get()).size(), 2u);
}

TEST(WriteAheadLogTest, TradingAfterRecoveryContinuesTheSameLog)
{
    WalFile file("append_after_replay");

    MatchingEngine original;
    original.enableWriteAheadLog(file.get());
    original.processOrder(Order(1, 100.0, 10, Side::BUY));
    original.disableWriteAheadLog();

    MatchingEngine recovered;
    recovered.replayFrom(file.get());
    recovered.enableWriteAheadLog(file.get());
    recovered.processOrder(Order(2, 99.0, 10, Side::BUY));
    recovered.disableWriteAheadLog();

    std::vector<WalRecord> records = WriteAheadLog::readAll(file.get());

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].order_id, 1);
    EXPECT_EQ(records[1].order_id, 2);
}
