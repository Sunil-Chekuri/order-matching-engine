// Concurrent stress harness.
//
// Separate from the unit suite on purpose: this runs hot and long, and
// it checks invariants rather than specific outcomes. Every invariant
// here is chosen to hold *regardless of thread interleaving*, which is
// what makes a concurrency test stable enough to trust — the same
// approach the Day 10 concurrency tests use.
//
// Exit code is non-zero if any invariant is violated.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "core/order.h"
#include "engine/matching_engine.h"
#include "utils/logger.h"

namespace
{
    int failures = 0;

    void check(
        bool condition,
        const std::string &what,
        long long expected,
        long long actual)
    {
        if (condition)
            return;

        ++failures;

        std::printf(
            "  VIOLATION %-38s expected %lld, got %lld\n",
            what.c_str(),
            expected,
            actual);
    }

    // Uniform quantities at a single price mean every match consumes
    // exactly one buy and one sell in full. So no matter how the threads
    // interleave, the totals are fixed: min(buys, sells) trades, and the
    // surplus side is left resting. Anything else means quantity or a
    // removal was lost to a race.
    void phaseSharedSymbol(int threads, int buys, int sells)
    {
        MatchingEngine engine;

        std::vector<std::thread> workers;

        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back(
                [&engine, t, buys, sells]()
                {
                    int id = t * 1000000 + 1;

                    for (int i = 0; i < buys; ++i)
                        engine.processOrder(
                            Order(id++, 100.0, 10, Side::BUY,
                                  OrderType::LIMIT, 0, "SHARED"));

                    for (int i = 0; i < sells; ++i)
                        engine.processOrder(
                            Order(id++, 100.0, 10, Side::SELL,
                                  OrderType::LIMIT, 0, "SHARED"));
                });
        }

        for (auto &worker : workers)
            worker.join();

        const long long total_buys = static_cast<long long>(threads) * buys;
        const long long total_sells = static_cast<long long>(threads) * sells;
        const long long expected_trades =
            total_buys < total_sells ? total_buys : total_sells;
        const long long surplus =
            total_buys > total_sells
                ? total_buys - total_sells
                : total_sells - total_buys;

        EngineMetrics metrics = engine.metrics();

        check(metrics.symbols == 1u, "shared: symbol count", 1, static_cast<long long>(metrics.symbols));
        check(metrics.orders_submitted == total_buys + total_sells, "shared: orders submitted", total_buys + total_sells, metrics.orders_submitted);
        check(metrics.trades_executed == expected_trades, "shared: trades executed", expected_trades, metrics.trades_executed);
        check(metrics.resting_orders == surplus, "shared: resting orders", surplus, metrics.resting_orders);
    }

    // Same invariant, but every thread on its own shard, so the books
    // never contend even though the shard map still does.
    void phasePerThreadSymbol(int threads, int buys, int sells)
    {
        MatchingEngine engine;

        std::vector<std::thread> workers;

        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back(
                [&engine, t, buys, sells]()
                {
                    const std::string symbol = "SYM" + std::to_string(t);
                    int id = 1;

                    for (int i = 0; i < buys; ++i)
                        engine.processOrder(
                            Order(id++, 100.0, 10, Side::BUY,
                                  OrderType::LIMIT, 0, symbol));

                    for (int i = 0; i < sells; ++i)
                        engine.processOrder(
                            Order(id++, 100.0, 10, Side::SELL,
                                  OrderType::LIMIT, 0, symbol));
                });
        }

        for (auto &worker : workers)
            worker.join();

        const long long per_symbol_trades = buys < sells ? buys : sells;
        const long long per_symbol_surplus =
            buys > sells ? buys - sells : sells - buys;

        EngineMetrics metrics = engine.metrics();

        check(metrics.symbols == static_cast<std::size_t>(threads), "sharded: symbol count", threads, static_cast<long long>(metrics.symbols));
        check(metrics.trades_executed == per_symbol_trades * threads, "sharded: trades executed", per_symbol_trades * threads, metrics.trades_executed);
        check(metrics.resting_orders == per_symbol_surplus * threads, "sharded: resting orders", per_symbol_surplus * threads, metrics.resting_orders);
    }

    // Nothing in this phase can match — every order is a BUY at the same
    // price — so the race under test is cancel against cancel: every
    // thread tries to cancel every order, and exactly one attempt per
    // order must win.
    void phaseCancelRace(int threads, int per_thread)
    {
        MatchingEngine engine;

        std::atomic<int> submitted{0};

        std::vector<std::thread> workers;

        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back(
                [&engine, &submitted, t, threads, per_thread]()
                {
                    int base = t * 1000000 + 1;

                    for (int i = 0; i < per_thread; ++i)
                        engine.processOrder(
                            Order(base + i, 100.0, 10, Side::BUY,
                                  OrderType::LIMIT, 0, "CANCELME"));

                    // No thread may start cancelling until every order
                    // exists. Without this barrier a fast thread sweeps
                    // past an id a slower thread has not submitted yet;
                    // that cancel is rejected legitimately, nobody
                    // retries, and the order rests forever — so
                    // "every order cancelled exactly once" would fail
                    // through interleaving alone rather than through any
                    // engine defect. Measured: 3 such false positives in
                    // 5000 runs before this was added.
                    ++submitted;

                    while (submitted.load() < threads)
                        std::this_thread::yield();

                    // Most attempts must lose the race and be rejected.
                    for (int t2 = 0; t2 < threads; ++t2)
                        for (int i = 0; i < per_thread; ++i)
                            engine.cancelOrder(t2 * 1000000 + 1 + i, "CANCELME");
                });
        }

        for (auto &worker : workers)
            worker.join();

        const long long orders = static_cast<long long>(threads) * per_thread;
        const long long attempts = orders * threads;

        EngineMetrics metrics = engine.metrics();

        check(metrics.cancels_accepted + metrics.cancels_rejected == attempts, "cancel: attempts accounted", attempts, metrics.cancels_accepted + metrics.cancels_rejected);
        check(metrics.cancels_accepted == orders, "cancel: each order cancelled once", orders, metrics.cancels_accepted);
        check(metrics.resting_orders == 0, "cancel: nothing left resting", 0, metrics.resting_orders);
    }

    // Readers must never observe a half-applied mutation: no negative or
    // zero quantities, and bids strictly descending in price.
    void phaseReadersDuringWrites(int writer_count, int per_writer)
    {
        MatchingEngine engine;

        std::atomic<bool> writers_done{false};
        std::atomic<int> malformed{0};
        std::atomic<int> reads{0};

        // The reader has to actually observe the book mid-write for
        // this phase to test anything, so the writers run until it has
        // taken a quota of readings rather than for a fixed count.
        // Gating only on "writers finished" is not enough: under load
        // the reader can be scheduled zero times before the writers are
        // done, which is a false failure rather than a race. This is
        // the same flaw Day 11 fixed in the Day 10 concurrency test,
        // reintroduced here and caught by running the harness 5000
        // times (4 false failures).
        const int read_quota = 50;

        std::vector<std::thread> workers;

        for (int t = 0; t < writer_count; ++t)
        {
            workers.emplace_back(
                [&engine, &reads, t, per_writer, read_quota]()
                {
                    int id = t * 1000000 + 1;

                    for (int i = 0; i < per_writer; ++i)
                        engine.processOrder(
                            Order(id++, 100.0 + (i % 25), 10, Side::BUY,
                                  OrderType::LIMIT, 0, "README"));

                    while (reads.load() < read_quota)
                        engine.processOrder(
                            Order(id++, 100.0 + (id % 25), 10, Side::BUY,
                                  OrderType::LIMIT, 0, "README"));
                });
        }

        std::thread reader(
            [&engine, &writers_done, &malformed, &reads]()
            {
                while (!writers_done)
                {
                    BookSnapshot snapshot = engine.snapshot(10, "README");

                    for (std::size_t i = 0; i < snapshot.bids.size(); ++i)
                    {
                        if (snapshot.bids[i].total_quantity <= 0 ||
                            snapshot.bids[i].order_count <= 0)
                            ++malformed;

                        if (i > 0 &&
                            snapshot.bids[i].price >= snapshot.bids[i - 1].price)
                            ++malformed;
                    }

                    EngineMetrics metrics = engine.metrics();

                    if (metrics.resting_orders < 0 ||
                        metrics.orders_submitted < 0 ||
                        metrics.trades_executed > metrics.orders_submitted)
                        ++malformed;

                    ++reads;
                }
            });

        for (auto &worker : workers)
            worker.join();

        writers_done = true;
        reader.join();

        check(malformed.load() == 0, "readers: malformed observations", 0, malformed.load());
        check(reads.load() >= read_quota, "readers: met reading quota", read_quota, reads.load());
    }
}

int main(int argc, char **argv)
{
    const int rounds = (argc > 1) ? std::atoi(argv[1]) : 20;
    const int threads = (argc > 2) ? std::atoi(argv[2]) : 4;

    // Keep the harness's own output clean; the engine's logging is not
    // what is under test here.
    Logger::setEnabled(false);

    std::printf("stress: %d rounds x %d threads\n", rounds, threads);

    for (int round = 1; round <= rounds; ++round)
    {
        const int before = failures;

        phaseSharedSymbol(threads, 60, 40);
        phasePerThreadSymbol(threads, 60, 40);
        phaseCancelRace(threads, 50);
        phaseReadersDuringWrites(threads, 400);

        if (failures != before)
            std::printf("round %d FAILED\n", round);
    }

    std::printf("stress: %d invariant violations\n", failures);

    return failures == 0 ? 0 : 1;
}
