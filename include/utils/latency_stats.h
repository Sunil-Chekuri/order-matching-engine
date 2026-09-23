#pragma once

#include <cstddef>
#include <vector>

// Collects individual latency samples and reports their distribution.
//
// Percentiles use the nearest-rank definition: for N sorted samples, the
// p-th percentile is the sample at rank ceil(p / 100 * N), 1-indexed.
// No interpolation between neighbouring samples is performed.
class LatencyStats
{
private:
    std::vector<long long> samples;

    bool sorted = true;

public:
    void reserve(
        std::size_t expected_samples);

    void record(
        long long nanoseconds);

    std::size_t count() const;

    long long percentile(
        double p);

    long long min() const;

    long long max() const;

    double mean() const;
};
