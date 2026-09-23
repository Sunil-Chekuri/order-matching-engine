#include <gtest/gtest.h>

#include "utils/latency_stats.h"

namespace
{
    LatencyStats statsFromOneToHundred()
    {
        LatencyStats stats;

        for (int i = 1; i <= 100; ++i)
            stats.record(i);

        return stats;
    }
}

TEST(LatencyStatsTest, StartsEmpty)
{
    LatencyStats stats;

    EXPECT_EQ(stats.count(), 0u);
}

TEST(LatencyStatsTest, CountsRecordedSamples)
{
    LatencyStats stats;
    stats.record(10);
    stats.record(20);
    stats.record(30);

    EXPECT_EQ(stats.count(), 3u);
}

TEST(LatencyStatsTest, ThrowsOnPercentileOfEmptySet)
{
    LatencyStats stats;

    EXPECT_THROW(stats.percentile(50.0), std::out_of_range);
}

TEST(LatencyStatsTest, ThrowsOnMinMaxMeanOfEmptySet)
{
    LatencyStats stats;

    EXPECT_THROW(stats.min(), std::out_of_range);
    EXPECT_THROW(stats.max(), std::out_of_range);
    EXPECT_THROW(stats.mean(), std::out_of_range);
}

TEST(LatencyStatsTest, ThrowsOnPercentileOutsideValidRange)
{
    LatencyStats stats;
    stats.record(10);

    EXPECT_THROW(stats.percentile(0.0), std::invalid_argument);
    EXPECT_THROW(stats.percentile(-5.0), std::invalid_argument);
    EXPECT_THROW(stats.percentile(100.1), std::invalid_argument);
}

TEST(LatencyStatsTest, SingleSampleIsEveryPercentile)
{
    LatencyStats stats;
    stats.record(42);

    EXPECT_EQ(stats.percentile(1.0), 42);
    EXPECT_EQ(stats.percentile(50.0), 42);
    EXPECT_EQ(stats.percentile(99.0), 42);
    EXPECT_EQ(stats.percentile(100.0), 42);
}

TEST(LatencyStatsTest, NearestRankPercentilesOnKnownDistribution)
{
    LatencyStats stats = statsFromOneToHundred();

    EXPECT_EQ(stats.percentile(1.0), 1);
    EXPECT_EQ(stats.percentile(50.0), 50);
    EXPECT_EQ(stats.percentile(95.0), 95);
    EXPECT_EQ(stats.percentile(99.0), 99);
    EXPECT_EQ(stats.percentile(100.0), 100);
}

TEST(LatencyStatsTest, PercentileRankRoundsUpToNextSample)
{
    // Ten samples: each represents exactly 10% of the distribution, so
    // any percentile inside a band must round up to that band's sample.
    LatencyStats stats;
    for (int i = 1; i <= 10; ++i)
        stats.record(i * 100);

    EXPECT_EQ(stats.percentile(30.0), 300); // exact rank 3
    EXPECT_EQ(stats.percentile(25.0), 300); // rank 2.5 rounds up to 3
    EXPECT_EQ(stats.percentile(99.0), 1000);
}

TEST(LatencyStatsTest, PercentilesIgnoreInsertionOrder)
{
    LatencyStats stats;
    stats.record(500);
    stats.record(100);
    stats.record(900);
    stats.record(300);
    stats.record(700);

    EXPECT_EQ(stats.percentile(20.0), 100);
    EXPECT_EQ(stats.percentile(60.0), 500);
    EXPECT_EQ(stats.percentile(100.0), 900);
}

TEST(LatencyStatsTest, SamplesRecordedAfterAPercentileAreStillCounted)
{
    // percentile() sorts and caches; recording afterwards must
    // invalidate that cache rather than reporting a stale ordering.
    LatencyStats stats;
    stats.record(10);
    stats.record(30);

    ASSERT_EQ(stats.percentile(100.0), 30);

    stats.record(20);
    stats.record(5);

    EXPECT_EQ(stats.count(), 4u);
    EXPECT_EQ(stats.percentile(25.0), 5);
    EXPECT_EQ(stats.percentile(100.0), 30);
}

TEST(LatencyStatsTest, MinMaxAndMeanAreCorrect)
{
    LatencyStats stats;
    stats.record(10);
    stats.record(20);
    stats.record(60);

    EXPECT_EQ(stats.min(), 10);
    EXPECT_EQ(stats.max(), 60);
    EXPECT_DOUBLE_EQ(stats.mean(), 30.0);
}

TEST(LatencyStatsTest, MeanIsNotTruncatedToAnInteger)
{
    // Regression guard for the integer-division bug this class replaced
    // in main.cpp, where a fractional average silently became a whole
    // number before it was ever stored in a double.
    LatencyStats stats;
    stats.record(1);
    stats.record(2);

    EXPECT_DOUBLE_EQ(stats.mean(), 1.5);
}
