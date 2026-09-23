#include <benchmark/benchmark.h>

#include "core/order.h"
#include "engine/matching_engine.h"
#include "engine/order_book.h"
#include "utils/logger.h"

// A buy and a sell that fully match each other, leaving the book empty
// again: steady-state cost of the complete submit-match-remove path.
static void BM_MatchingPair(benchmark::State &state)
{
    MatchingEngine engine;

    int id = 0;

    for (auto _ : state)
    {
        engine.processOrder(Order(++id, 100.0, 10, Side::BUY));
        engine.processOrder(Order(++id, 100.0, 10, Side::SELL));
    }

    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_MatchingPair);

// Buys with no asks to trade against: pure insertion into a single
// price level whose FIFO queue grows for the whole run.
static void BM_RestingInsert(benchmark::State &state)
{
    MatchingEngine engine;

    int id = 0;

    for (auto _ : state)
    {
        engine.processOrder(Order(++id, 100.0, 10, Side::BUY));
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RestingInsert)->Iterations(200000);

// Submit then immediately cancel, so the book stays shallow and the
// measurement reflects registry lookup plus removal rather than depth.
static void BM_SubmitThenCancel(benchmark::State &state)
{
    MatchingEngine engine;

    int id = 0;

    for (auto _ : state)
    {
        ++id;
        engine.processOrder(Order(id, 100.0, 10, Side::BUY));
        benchmark::DoNotOptimize(engine.cancelOrder(id));
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SubmitThenCancel);

// FOK's pre-check walks every crossing price level before deciding
// whether to trade. Sweeping book depth shows how that cost scales.
static void BM_AvailableToMatch(benchmark::State &state)
{
    const int levels = static_cast<int>(state.range(0));

    OrderBook book;

    for (int i = 0; i < levels; ++i)
        book.addOrder(Order(i + 1, 100.0 + i, 10, Side::SELL));

    const double limit_price = 100.0 + levels;

    for (auto _ : state)
    {
        benchmark::DoNotOptimize(
            book.availableToMatch(Side::BUY, limit_price));
    }
}
BENCHMARK(BM_AvailableToMatch)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

int main(int argc, char **argv)
{
    // cancelOrder() writes a log line, with a flush, on every single
    // call. Left on, that console I/O would dwarf the engine work these
    // benchmarks exist to measure.
    Logger::setEnabled(false);

    benchmark::Initialize(&argc, argv);

    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
