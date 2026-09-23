#include "utils/timer.h"

void Timer::start()
{
    start_time =
        std::chrono::high_resolution_clock::now();
}

long long Timer::stop()
{
    auto end_time =
        std::chrono::high_resolution_clock::now();

    return std::chrono::duration_cast<
               std::chrono::microseconds>(end_time - start_time)
        .count();
}

long long Timer::stopNanos()
{
    auto end_time =
        std::chrono::high_resolution_clock::now();

    return std::chrono::duration_cast<
               std::chrono::nanoseconds>(end_time - start_time)
        .count();
}