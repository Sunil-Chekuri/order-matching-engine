#include <gtest/gtest.h>

#include <thread>

#include "utils/timer.h"

TEST(TimerTest, StopAfterStartReturnsNonNegativeDuration)
{
    Timer timer;
    timer.start();

    long long elapsed = timer.stop();

    EXPECT_GE(elapsed, 0);
}

TEST(TimerTest, StopReflectsElapsedTime)
{
    Timer timer;
    timer.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    long long elapsed_us = timer.stop();

    // Allow generous slack for scheduler jitter, but the measured
    // duration must be at least in the right ballpark of the sleep.
    EXPECT_GE(elapsed_us, 10000);
    EXPECT_LT(elapsed_us, 1000000);
}
