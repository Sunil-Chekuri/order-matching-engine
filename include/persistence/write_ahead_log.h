#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "core/order.h"

// One durable record of something the engine was asked to do.
//
// The log stores commands (what arrived), not results (what happened).
// Matching is deterministic given an input sequence, so replaying the
// commands in order reconstructs the books and regenerates every trade.
// Storing results instead would be larger and would still have to agree
// with the engine's logic, so it buys nothing here.
struct WalRecord
{
    enum class Kind
    {
        SUBMIT,
        CANCEL
    };

    Kind kind = Kind::SUBMIT;

    int order_id = 0;

    // Meaningful for SUBMIT only; a CANCEL carries order_id and symbol.
    double price = 0.0;
    int quantity = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    int participant_id = 0;

    std::string symbol;
};

// Append-only command log.
//
// Records are written before the engine applies the corresponding change,
// which is the entire point of "write-ahead": a change that survived a
// crash is always one the log already describes, never the reverse.
//
// Durability caveat: appends are flushed to the operating system, not
// forced to the physical device. A power loss can still lose recently
// written records. Real durability needs an fsync per commit (or per
// batch), which costs far more than the flush measured on Day 12.
class WriteAheadLog
{
private:
    std::ofstream file;

    // Shards append concurrently into one file, so the log needs its own
    // lock. That makes it a global serialisation point across every
    // symbol — see docs/DAILY_LOG.md (Day 12) for the measured cost.
    mutable std::mutex mutex;

public:
    void open(
        const std::string &path);

    bool isOpen() const;

    void appendSubmit(
        const Order &order);

    void appendCancel(
        int order_id,
        const std::string &symbol);

    void close();

    // Reads a log back in the order it was written. Lines that cannot be
    // parsed are skipped rather than throwing: a log truncated mid-write
    // by a crash is an expected condition, not a corrupt file.
    static std::vector<WalRecord> readAll(
        const std::string &path);
};
