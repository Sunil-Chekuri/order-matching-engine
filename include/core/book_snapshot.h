#pragma once

#include <string>
#include <vector>

// One aggregated price level: every resting order at this price collapsed
// into a single total. This is a market-by-price (L2) view — individual
// order identities are deliberately not exposed, which is exactly what a
// public depth feed publishes, as opposed to the market-by-order (L3)
// view the engine itself works with internally.
struct PriceLevel
{
    double price;

    int total_quantity;

    int order_count;
};

// Top-of-book view of both sides, best price first: bids descending,
// asks ascending.
struct BookSnapshot
{
    std::vector<PriceLevel> bids;

    std::vector<PriceLevel> asks;
};

// Machine-readable single-line rendering, matching the shape toJsonLine
// already produces for EngineMetrics so a consumer parses both the same
// way. Prices are written with enough significant digits to survive the
// round trip through text — std::to_string would truncate to six
// decimals and report a level at a price that is not the one the book
// is holding.
std::string toJsonLine(
    const BookSnapshot &snapshot);
