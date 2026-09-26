#include <gtest/gtest.h>

#include "cli/depth_view.h"

#include <string>

namespace
{
    BookSnapshot makeSnapshot()
    {
        BookSnapshot snapshot;

        snapshot.bids.push_back(PriceLevel{100.5, 150, 2});
        snapshot.bids.push_back(PriceLevel{100.25, 40, 1});

        snapshot.asks.push_back(PriceLevel{100.75, 80, 1});
        snapshot.asks.push_back(PriceLevel{101.0, 220, 3});

        return snapshot;
    }

    bool contains(
        const std::string &haystack,
        const std::string &needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

TEST(DepthViewTest, ParsesABothSidedSnapshotReply)
{
    BookSnapshot snapshot;
    std::string error;

    ASSERT_TRUE(parseSnapshotReply(
        "OK {\"bids\":[{\"price\":100.5,\"quantity\":150,\"orders\":2}],"
        "\"asks\":[{\"price\":100.75,\"quantity\":80,\"orders\":1}]}",
        snapshot,
        error))
        << error;

    ASSERT_EQ(snapshot.bids.size(), 1u);
    ASSERT_EQ(snapshot.asks.size(), 1u);

    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 100.5);
    EXPECT_EQ(snapshot.bids[0].total_quantity, 150);
    EXPECT_EQ(snapshot.bids[0].order_count, 2);

    EXPECT_DOUBLE_EQ(snapshot.asks[0].price, 100.75);
    EXPECT_EQ(snapshot.asks[0].total_quantity, 80);
    EXPECT_EQ(snapshot.asks[0].order_count, 1);
}

TEST(DepthViewTest, ParsesEmptyArrays)
{
    BookSnapshot snapshot;
    std::string error;

    ASSERT_TRUE(parseSnapshotReply(
        "OK {\"bids\":[],\"asks\":[]}", snapshot, error))
        << error;

    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(DepthViewTest, ParsesAOneSidedBook)
{
    BookSnapshot snapshot;
    std::string error;

    ASSERT_TRUE(parseSnapshotReply(
        "OK {\"bids\":[{\"price\":99,\"quantity\":5,\"orders\":1}],\"asks\":[]}",
        snapshot,
        error))
        << error;

    EXPECT_EQ(snapshot.bids.size(), 1u);
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(DepthViewTest, ParsesSeveralLevelsInOrder)
{
    BookSnapshot snapshot;
    std::string error;

    ASSERT_TRUE(parseSnapshotReply(
        "OK {\"bids\":["
        "{\"price\":99,\"quantity\":5,\"orders\":1},"
        "{\"price\":98,\"quantity\":6,\"orders\":2},"
        "{\"price\":97,\"quantity\":7,\"orders\":3}],"
        "\"asks\":[]}",
        snapshot,
        error))
        << error;

    ASSERT_EQ(snapshot.bids.size(), 3u);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 99.0);
    EXPECT_DOUBLE_EQ(snapshot.bids[2].price, 97.0);
    EXPECT_EQ(snapshot.bids[2].order_count, 3);
}

TEST(DepthViewTest, SurfacesTheServersOwnErrorReason)
{
    // The client should report what the server said, not invent its own
    // wording for it.
    BookSnapshot snapshot;
    std::string error;

    EXPECT_FALSE(parseSnapshotReply("ERR bad_depth", snapshot, error));
    EXPECT_EQ(error, "bad_depth");
}

TEST(DepthViewTest, RejectsRepliesThatAreNeitherOkNorErr)
{
    BookSnapshot snapshot;
    std::string error;

    EXPECT_FALSE(parseSnapshotReply("", snapshot, error));
    EXPECT_FALSE(parseSnapshotReply("garbage", snapshot, error));
    EXPECT_FALSE(error.empty());
}

TEST(DepthViewTest, RejectsMalformedPayloads)
{
    BookSnapshot snapshot;
    std::string error;

    // Missing an array entirely.
    EXPECT_FALSE(parseSnapshotReply("OK {\"bids\":[]}", snapshot, error));

    // Unterminated array.
    EXPECT_FALSE(parseSnapshotReply(
        "OK {\"bids\":[{\"price\":1,\"quantity\":1,\"orders\":1}",
        snapshot, error));

    // A level missing one of its three fields.
    EXPECT_FALSE(parseSnapshotReply(
        "OK {\"bids\":[{\"price\":1,\"quantity\":1}],\"asks\":[]}",
        snapshot, error));

    EXPECT_FALSE(error.empty());
}

TEST(DepthViewTest, AFailedParseLeavesTheOutputUntouched)
{
    // The caller keeps rendering the last good frame, so a half-filled
    // snapshot on a bad reply would silently corrupt the display rather
    // than being reported.
    BookSnapshot snapshot = makeSnapshot();
    std::string error;

    EXPECT_FALSE(parseSnapshotReply("ERR bad_depth", snapshot, error));

    EXPECT_EQ(snapshot.bids.size(), 2u);
    EXPECT_EQ(snapshot.asks.size(), 2u);
}

TEST(DepthViewTest, ParsesMetricsReply)
{
    EngineMetrics metrics;
    std::string error;

    ASSERT_TRUE(parseMetricsReply(
        "OK {\"orders_submitted\":20,\"trades_executed\":7,"
        "\"cancels_accepted\":3,\"cancels_rejected\":1,"
        "\"resting_orders\":6,\"symbols\":2}",
        metrics,
        error))
        << error;

    EXPECT_EQ(metrics.orders_submitted, 20);
    EXPECT_EQ(metrics.trades_executed, 7);
    EXPECT_EQ(metrics.cancels_accepted, 3);
    EXPECT_EQ(metrics.cancels_rejected, 1);
    EXPECT_EQ(metrics.resting_orders, 6);
    EXPECT_EQ(metrics.symbols, 2u);
}

TEST(DepthViewTest, MetricsRejectsAMissingField)
{
    EngineMetrics metrics;
    std::string error;

    EXPECT_FALSE(parseMetricsReply(
        "OK {\"orders_submitted\":20,\"trades_executed\":7}", metrics, error));

    EXPECT_FALSE(error.empty());
}

TEST(DepthViewTest, SnapshotSurvivesTheFullServerToClientRoundTrip)
{
    // The test that matters most here: it closes the loop between what
    // the server emits and what this client reads back. A format change
    // on either side breaks it, which neither side's own tests would
    // catch on their own.
    const BookSnapshot original = makeSnapshot();

    BookSnapshot parsed;
    std::string error;

    ASSERT_TRUE(parseSnapshotReply(
        "OK " + toJsonLine(original), parsed, error))
        << error;

    ASSERT_EQ(parsed.bids.size(), original.bids.size());
    ASSERT_EQ(parsed.asks.size(), original.asks.size());

    for (std::size_t i = 0; i < original.bids.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(parsed.bids[i].price, original.bids[i].price);
        EXPECT_EQ(parsed.bids[i].total_quantity, original.bids[i].total_quantity);
        EXPECT_EQ(parsed.bids[i].order_count, original.bids[i].order_count);
    }

    for (std::size_t i = 0; i < original.asks.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(parsed.asks[i].price, original.asks[i].price);
        EXPECT_EQ(parsed.asks[i].total_quantity, original.asks[i].total_quantity);
        EXPECT_EQ(parsed.asks[i].order_count, original.asks[i].order_count);
    }
}

TEST(DepthViewTest, AwkwardPriceSurvivesTheRoundTrip)
{
    // Same guarantee the wire format was given on Day 15, checked from
    // the reading end: a price that is not exactly representable must
    // come back as the identical double, not a rounded neighbour.
    BookSnapshot original;
    original.bids.push_back(PriceLevel{100.1234567890123, 5, 1});

    BookSnapshot parsed;
    std::string error;

    ASSERT_TRUE(parseSnapshotReply("OK " + toJsonLine(original), parsed, error))
        << error;

    ASSERT_EQ(parsed.bids.size(), 1u);
    EXPECT_EQ(parsed.bids[0].price, original.bids[0].price);
}

TEST(DepthViewTest, MetricsSurviveTheFullRoundTrip)
{
    EngineMetrics original;
    original.orders_submitted = 123;
    original.trades_executed = 45;
    original.cancels_accepted = 6;
    original.cancels_rejected = 7;
    original.resting_orders = 8;
    original.symbols = 9;

    EngineMetrics parsed;
    std::string error;

    ASSERT_TRUE(parseMetricsReply("OK " + toJsonLine(original), parsed, error))
        << error;

    EXPECT_EQ(parsed.orders_submitted, original.orders_submitted);
    EXPECT_EQ(parsed.trades_executed, original.trades_executed);
    EXPECT_EQ(parsed.cancels_accepted, original.cancels_accepted);
    EXPECT_EQ(parsed.cancels_rejected, original.cancels_rejected);
    EXPECT_EQ(parsed.resting_orders, original.resting_orders);
    EXPECT_EQ(parsed.symbols, original.symbols);
}

TEST(DepthViewTest, RenderShowsBothSidesAndTheSymbol)
{
    const std::string frame = renderDepth("ACME", makeSnapshot());

    EXPECT_TRUE(contains(frame, "ACME")) << frame;
    EXPECT_TRUE(contains(frame, "100.5")) << frame;
    EXPECT_TRUE(contains(frame, "100.75")) << frame;
    EXPECT_TRUE(contains(frame, "150")) << frame;
    EXPECT_TRUE(contains(frame, "220")) << frame;
}

TEST(DepthViewTest, RenderReportsSpreadAndMidWhenBothSidesExist)
{
    const std::string frame = renderDepth("ACME", makeSnapshot());

    // Best bid 100.5, best ask 100.75.
    EXPECT_TRUE(contains(frame, "spread 0.25")) << frame;
    EXPECT_TRUE(contains(frame, "mid 100.625")) << frame;
}

TEST(DepthViewTest, RenderOmitsSpreadWhenOneSideIsEmpty)
{
    // A spread against an empty side would be an invented number.
    BookSnapshot one_sided;
    one_sided.bids.push_back(PriceLevel{99.0, 5, 1});

    const std::string frame = renderDepth("ACME", one_sided);

    EXPECT_FALSE(contains(frame, "spread")) << frame;
    EXPECT_FALSE(contains(frame, "mid")) << frame;
}

TEST(DepthViewTest, RenderHandlesAnEmptyBook)
{
    const std::string frame = renderDepth("QUIET", BookSnapshot{});

    EXPECT_TRUE(contains(frame, "QUIET")) << frame;
    EXPECT_TRUE(contains(frame, "(empty)")) << frame;
    EXPECT_FALSE(contains(frame, "spread")) << frame;
}

TEST(DepthViewTest, RenderPadsTheShorterSideSoRowsStayAligned)
{
    // Three asks against one bid: every row must still have the same
    // width up to the divider, or the ladder visibly skews.
    BookSnapshot lopsided;
    lopsided.bids.push_back(PriceLevel{99.0, 5, 1});
    lopsided.asks.push_back(PriceLevel{100.0, 5, 1});
    lopsided.asks.push_back(PriceLevel{101.0, 5, 1});
    lopsided.asks.push_back(PriceLevel{102.0, 5, 1});

    const std::string frame = renderDepth("ACME", lopsided);

    std::size_t rows_with_divider = 0;
    std::size_t divider_column = std::string::npos;

    std::size_t start = 0;

    while (start < frame.size())
    {
        const std::size_t newline = frame.find('\n', start);
        const std::string row =
            frame.substr(start, newline == std::string::npos
                                    ? std::string::npos
                                    : newline - start);

        const std::size_t at = row.find("  |  ");

        if (at != std::string::npos)
        {
            ++rows_with_divider;

            if (divider_column == std::string::npos)
                divider_column = at;
            else
                EXPECT_EQ(at, divider_column) << "row: [" << row << "]";
        }

        if (newline == std::string::npos)
            break;

        start = newline + 1;
    }

    // Header plus three data rows.
    EXPECT_GE(rows_with_divider, 4u) << frame;
}

TEST(DepthViewTest, LadderPricesAllShareTheSameDecimalPlaces)
{
    // Rendered at their own natural precision these would read 100.5,
    // 100.75 and 101 -- a ragged column, which defeats the one reading
    // a depth ladder exists to make easy.
    BookSnapshot snapshot;
    snapshot.bids.push_back(PriceLevel{100.5, 10, 1});
    snapshot.bids.push_back(PriceLevel{100.0, 10, 1});
    snapshot.asks.push_back(PriceLevel{100.75, 10, 1});
    snapshot.asks.push_back(PriceLevel{101.0, 10, 1});

    const std::string frame = renderDepth("ACME", snapshot);

    EXPECT_TRUE(contains(frame, "100.50")) << frame;
    EXPECT_TRUE(contains(frame, "100.00")) << frame;
    EXPECT_TRUE(contains(frame, "100.75")) << frame;
    EXPECT_TRUE(contains(frame, "101.00")) << frame;
}

TEST(DepthViewTest, LadderUsesNoDecimalsWhenNoPriceNeedsThem)
{
    // The width follows the data rather than being hardcoded, so whole
    // -numbered prices are not padded with meaningless zeros.
    BookSnapshot snapshot;
    snapshot.bids.push_back(PriceLevel{99.0, 10, 1});
    snapshot.asks.push_back(PriceLevel{101.0, 10, 1});

    const std::string frame = renderDepth("ACME", snapshot);

    EXPECT_TRUE(contains(frame, "99 ")) << frame;
    EXPECT_FALSE(contains(frame, "99.0")) << frame;
}

TEST(DepthViewTest, RenderMetricsShowsEveryCounter)
{
    EngineMetrics metrics;
    metrics.orders_submitted = 123;
    metrics.trades_executed = 45;
    metrics.cancels_accepted = 6;
    metrics.cancels_rejected = 7;
    metrics.resting_orders = 8;
    metrics.symbols = 9;

    const std::string line = renderMetrics(metrics);

    EXPECT_TRUE(contains(line, "orders 123")) << line;
    EXPECT_TRUE(contains(line, "trades 45")) << line;
    EXPECT_TRUE(contains(line, "resting 8")) << line;
    EXPECT_TRUE(contains(line, "cancels 6/7")) << line;
    EXPECT_TRUE(contains(line, "symbols 9")) << line;
}
