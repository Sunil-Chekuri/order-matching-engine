#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/order_book.h"
#include "core/trade.h"
#include "core/engine_metrics.h"
#include "persistence/write_ahead_log.h"

class MatchingEngine
{
private:
    // One book per instrument, each behind its own lock. Orders for
    // different symbols never touch the same mutex, which is the whole
    // point: a single global lock made throughput *fall* as threads were
    // added (measured on Day 10).
    struct SymbolBook
    {
        OrderBook book;

        std::mutex mutex;

        // Counts trades on this symbol and doubles as the source of
        // trade ids, so ids are unique per symbol rather than globally.
        // Acceptable while trades are not persisted anywhere; to be
        // revisited when the Day 12 write-ahead log gives them meaning.
        int total_trades = 0;

        // Observability counters. Kept per shard and summed on read,
        // rather than as shared atomics, for the same reason the books
        // are sharded: a single counter touched by every order would put
        // one cache line back in the middle of the hot path. These are
        // already covered by the shard lock, so they cost an increment.
        long long orders_submitted = 0;
        long long cancels_accepted = 0;
        long long cancels_rejected = 0;
    };

    // Held by unique_ptr for two reasons: std::mutex is neither copyable
    // nor movable, and the pointee must stay put when the map rehashes
    // so that a shard reference remains valid after books_mutex has been
    // released. Shards are never erased.
    std::unordered_map<std::string, std::unique_ptr<SymbolBook>> books;

    // Guards the map of shards, not the books inside them.
    //
    // This is the current bottleneck, knowingly. Every order takes this
    // lock briefly to find its shard, so all symbols still serialise on
    // one mutex — just for a hash lookup rather than for the whole
    // match. Measured on Day 11: sharding still roughly doubled
    // two-thread throughput over a single global lock, but throughput
    // continues to fall as threads are added instead of scaling.
    //
    // A std::shared_mutex here would let lookups proceed concurrently
    // and is the obvious fix, but that version showed rare order-book
    // corruption under stress that is not yet explained (a standalone
    // probe cleared std::shared_mutex itself, so the fault is believed
    // to be in this code, not the toolchain). Shipping the version that
    // is provably correct until that is understood.
    mutable std::mutex books_mutex;

    // Optional. When no log is open every append is a no-op, so the
    // engine behaves exactly as it did before persistence existed.
    WriteAheadLog wal;

    // Set while replaying a log, to stop replayed commands being written
    // straight back into the log they came from.
    bool replaying = false;

    SymbolBook &getOrCreateShard(
        const std::string &symbol);

    SymbolBook *findShard(
        const std::string &symbol);

    // The two helpers below require shard.mutex to already be held by
    // the caller, and must never acquire it themselves: public entry
    // points lock exactly once, and a second acquisition of a
    // non-recursive mutex on the same thread would deadlock.
    void processOrderLocked(
        SymbolBook &shard,
        const Order &order);

    void matchAggressively(
        SymbolBook &shard,
        Order incoming,
        bool respect_price);

public:
    // The symbol comes from the order itself.
    void processOrder(
        const Order &order);

    // Cancelling requires knowing which instrument the order is on. That
    // is deliberate: the alternative, a global order-id-to-symbol index,
    // would be a single shared structure touched on every insert and
    // every cancel — reintroducing exactly the contention bottleneck
    // that sharding exists to remove. Real venues put the instrument in
    // the cancel message for the same reason.
    bool cancelOrder(
        int order_id,
        const std::string &symbol = DEFAULT_SYMBOL);

    bool getRemainingQuantity(
        int order_id,
        int &out_quantity,
        const std::string &symbol = DEFAULT_SYMBOL);

    BookSnapshot snapshot(
        std::size_t depth,
        const std::string &symbol = DEFAULT_SYMBOL);

    // Sums each shard in turn, locking them one at a time, so the result
    // is a consistent total only when nothing is trading concurrently —
    // it is a metrics counter, not a point-in-time snapshot.
    int getTotalTrades() const;

    std::size_t symbolCount() const;

    // Aggregated across shards, locking each briefly in turn — the same
    // eventually-consistent reading getTotalTrades() gives, for the same
    // reason. Cheap enough to scrape on a timer, and deliberately not
    // called from anywhere on the matching path.
    EngineMetrics metrics() const;

    // Starts recording commands to disk. Existing log content is kept,
    // so reopening the same path continues the same history.
    void enableWriteAheadLog(
        const std::string &path);

    void disableWriteAheadLog();

    // Rebuilds state by re-applying a log's commands in recorded order.
    // Intended for a fresh engine at startup; commands applied during
    // replay are not themselves logged. Returns how many were applied.
    std::size_t replayFrom(
        const std::string &path);
};
