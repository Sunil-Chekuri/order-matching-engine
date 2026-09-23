#include "utils/latency_stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

void LatencyStats::reserve(
    std::size_t expected_samples)
{
    samples.reserve(expected_samples);
}

void LatencyStats::record(
    long long nanoseconds)
{
    samples.push_back(nanoseconds);

    sorted = false;
}

std::size_t LatencyStats::count() const
{
    return samples.size();
}

long long LatencyStats::percentile(
    double p)
{
    if (samples.empty())
        throw std::out_of_range("No latency samples recorded");

    if (p <= 0.0 || p > 100.0)
        throw std::invalid_argument("Percentile must be within (0, 100]");

    if (!sorted)
    {
        std::sort(samples.begin(), samples.end());

        sorted = true;
    }

    // Multiply before dividing: (p / 100) * n would introduce a rounding
    // error that can push an exact rank just above a whole number and
    // shift the result a full sample too far.
    double exact_rank =
        (p * static_cast<double>(samples.size())) / 100.0;

    std::size_t rank =
        static_cast<std::size_t>(std::ceil(exact_rank));

    if (rank < 1)
        rank = 1;

    if (rank > samples.size())
        rank = samples.size();

    return samples[rank - 1];
}

long long LatencyStats::min() const
{
    if (samples.empty())
        throw std::out_of_range("No latency samples recorded");

    return *std::min_element(samples.begin(), samples.end());
}

long long LatencyStats::max() const
{
    if (samples.empty())
        throw std::out_of_range("No latency samples recorded");

    return *std::max_element(samples.begin(), samples.end());
}

double LatencyStats::mean() const
{
    if (samples.empty())
        throw std::out_of_range("No latency samples recorded");

    long long total =
        std::accumulate(samples.begin(), samples.end(), 0LL);

    return static_cast<double>(total) / static_cast<double>(samples.size());
}
