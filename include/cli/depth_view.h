#pragma once

#include <string>

#include "core/book_snapshot.h"
#include "core/engine_metrics.h"

// Reading the server's replies back, and turning them into something a
// person can read.
//
// Split out from the CLI loop for the same reason the protocol was split
// from the transport on Day 15: parsing and rendering are where the bugs
// are, and neither needs a socket or a terminal to exercise. The loop in
// cli/depth_cli.cpp is then thin enough to check by running it.

// Parses the reply to SNAPSHOT back into a BookSnapshot.
//
// This is NOT a general JSON parser, and deliberately so. It understands
// exactly the shape toJsonLine(BookSnapshot) emits — flat objects of
// three numeric fields, no nesting, no strings, no escapes, no unicode.
// Writing a real JSON parser to read one known fixed shape would be a
// large amount of code with a large surface for subtle bugs, in service
// of generality nothing here needs. The narrowness is the safety
// property: anything that is not that shape is rejected rather than
// half-understood.
//
// Returns false and fills `error` on a malformed reply or on an "ERR"
// reply from the server (in which case `error` carries the server's own
// reason, so a caller can report it verbatim).
bool parseSnapshotReply(
    const std::string &reply,
    BookSnapshot &out,
    std::string &error);

// Same contract for the reply to METRICS.
bool parseMetricsReply(
    const std::string &reply,
    EngineMetrics &out,
    std::string &error);

// The depth ladder: bids descending on the left, asks ascending on the
// right, best prices adjacent in the middle so the spread is the gap
// you actually look at. Ends with a spread/mid line when both sides
// have liquidity.
//
// ASCII only, deliberately: box-drawing characters render as mojibake
// in a Windows console that is not on a UTF-8 code page, and a depth
// display that looks broken on the machine this project is built on
// would be a poor demonstration.
std::string renderDepth(
    const std::string &symbol,
    const BookSnapshot &snapshot);

// One-line counter summary, rendered from the same reading the engine
// reports. Rates are not shown: a rate needs two readings and the
// interval between them, which is a property of the caller's loop, not
// of any single snapshot.
std::string renderMetrics(
    const EngineMetrics &metrics);
