# Design Notes

Working reference for module ownership and the reasoning behind design decisions. The
README carries the summary and the measured numbers; this file is where the longer "why"
lives, including the decisions that were reversed and the ones that were deliberately not
taken.

## Module ownership

- **core** (`Order`, `Trade`, `BookSnapshot`, `EngineMetrics`) — plain data types whose only
  behavior is construction-time validation. No dependencies on other modules.
  `BookSnapshot` lives here rather than in `engine` so the network and CLI layers can
  consume market data without including engine internals.
- **engine/OrderBook** — owns resting order state for a single symbol: two
  `std::map<price, std::deque<Order>>` (bids descending, asks ascending), plus an
  `order_id -> Order*` registry for O(1) cancellation lookup. Knows nothing about matching,
  only book structure.
- **engine/MatchingEngine** — owns the matching loop, order-type dispatch, self-trade
  prevention, and the per-symbol shards. Knows nothing about how orders arrive.
- **api/OrderGateway** — the external-facing entry point and the stable seam the network
  layer plugs into. Exposes `submitOrder`, `cancelOrder`, `snapshot`,
  `getRemainingQuantity` and `metrics`.
- **api/protocol** — decides what a request *means*. A function from string to string with
  no socket anywhere.
- **net/TcpServer, net/TcpClient** — transport only: accept, reframe the byte stream into
  lines, write the reply back. `src/net/socket_compat.h` confines every Winsock/BSD
  difference and stays out of `include/` so nothing else pulls in `<winsock2.h>`.
- **persistence/WriteAheadLog** — append-only command log plus replay.
- **cli/DepthView** — reply parsing and ladder layout, separated from the terminal so both
  are unit testable.
- **utils** (`Logger`, `Timer`, `LatencyStats`) — cross-cutting infrastructure, no domain
  knowledge.

## Decisions

### Data structures

- **`std::map` for price levels, not a hash map.** Price levels need ordered iteration
  ("best bid" = highest price) on every match attempt, which a hash map cannot give without
  a re-sort. The cost is real and measured: per-level walk cost degrades from 3.84 ns to
  7.59 ns between depth 10 and depth 1000 as node-based traversal outgrows cache.
- **Bids use `std::greater<double>`, asks default ascending**, so `begin()` means "best
  price" on both sides with no logic at the call site.
- **`order_registry` stores raw `Order*` into the deque.** Avoids a linear scan across price
  levels on cancel, at the cost of needing pointer discipline — `std::deque` guarantees
  stability under `push_back`/`pop_front`. Every removal path must erase its registry entry;
  missing that on the two "remove best" paths was a real dangling-pointer bug.
- **`matchAggressively` takes its order by value**, deliberately: it needs a private mutable
  remaining-quantity that must never leak back to the caller or into the book.

### Concurrency

- **The lock lives in `MatchingEngine`, not `OrderBook`.** This is the counterintuitive one.
  `bestBid()`/`bestAsk()` return *references* into deques the matching loop mutates across
  several separate calls, so a per-method mutex inside the book would release the lock while
  a live reference was still held — turning a fixed dangling-pointer bug into a race. A
  comment at the top of `order_book.h` records this so the "obvious fix" is not applied
  later.
- **Private helpers never lock.** The shard mutex is non-recursive, so a second acquisition
  deadlocks. `processOrder` is a locking public entry point over a non-locking
  `processOrderLocked`.
- **Per-symbol shards, each with its own mutex, held for a whole operation.** Shards are
  `unique_ptr` because `std::mutex` is immovable and the pointee must survive map rehashes.
- **Cancel requires a symbol.** A global id→symbol index would reintroduce precisely the
  shared bottleneck sharding exists to remove.
- **Counters are per shard and summed on read, not shared atomics.** One atomic touched by
  every order would put a contended cache line on the hot path. Inside the existing shard
  lock, each costs an increment.
- **The shard lookup uses a plain `std::mutex`, not `std::shared_mutex`.** The reader-writer
  version is the obvious optimisation and it is broken — it intermittently constructs two
  shards for one symbol, has lost submitted orders outright, and has died with an access
  violation from a corrupted map. Root cause unresolved after five investigations; five
  standalone probes of `std::shared_mutex` on this toolchain came back clean, so the fault
  is not understood. Do not reintroduce it until it is. This is also the throughput ceiling:
  every order serialises here for a hash lookup.
- **The server holds no locks of its own.** Thread per connection over one shared gateway is
  only sound because the engine shards per symbol and holds a shard lock across each whole
  operation.

### Persistence

- **The WAL logs commands, not results.** Matching is deterministic given an input sequence,
  so replaying submits and cancels regenerates every trade and resting order. Half the write
  volume of logging fills, and no possibility of a log whose trades disagree with its
  orders.
- **Appends happen inside the shard lock, before the book changes**, which is what makes log
  order match application order per symbol. Per-symbol ordering is sufficient, because a
  book depends only on its own commands.
- **Flush per record, accepted at ~28x throughput cost.** The records a crash needs are
  exactly the ones still in the user-space buffer, so dropping the flush would trade the
  entire guarantee for 4.6x. Flushing still only reaches the OS — power loss needs `fsync`,
  which is not done.
- **A malformed or truncated final line is skipped, not fatal.** A truncated last record is
  what a crash looks like.
- **Prices are written with `max_digits10`.** `std::to_string` truncates to six decimals,
  which would replay an order onto a different price level.
- **A `replaying` flag stops recovered commands being written back** into the log they came
  from, which would otherwise double the log on every recovery.

### Market data

- **Snapshots are L2 (market-by-price), not L3.** Orders at a price aggregate into one row
  with `order_count` as the only hint of internal composition, which is what a public depth
  feed publishes.
- **Depth 0 returns an empty snapshot rather than throwing**, unlike `bestBid()` on an empty
  book — an empty result is a sensible answer to "give me zero levels", whereas "the best
  price of nothing" is not.
- **Rates are not stored on the metrics snapshot.** A rate is a property of two readings and
  the interval between them, so it belongs to whoever polls.

### Interfaces and protocol

- **Additive extension with defaulted parameters.** `OrderType`, `participant_id` and
  `symbol` were each added as trailing constructor parameters with defaults so every
  existing call site kept compiling and behaving identically.
- **Protocol split from transport.** Everything that can be wrong about a request is decided
  by a pure-ish function over a gateway, which is where the interesting bugs live; the
  transport is then thin enough for a handful of round-trip tests. It also means an event
  loop could replace `tcp_server.cpp` wholesale without touching `protocol.cpp`.
- **Line-based ASCII over binary framing.** A length-prefixed binary format would skip the
  parse, but the parse is ~4 µs against ~23 µs of loopback round trip, and text can be
  driven from netcat during a demo.
- **Error reasons are fixed lowercase tokens, not prose**, so a client can branch on them
  without parsing English.
- **The snapshot reply parser is deliberately not a JSON parser.** It understands exactly
  the flat three-numeric-field shape `toJsonLine` emits — no nesting, strings, escapes or
  unicode. The narrowness is the safety property: anything that is not that shape is
  rejected rather than half-understood.

### Portability

- **Every Winsock/BSD difference lives in `src/net/socket_compat.h`**, and it is deliberately
  not in `include/` so nothing outside the transport ever pulls in `<winsock2.h>`.
- **The accept loop polls with `select()` instead of blocking in `accept()`.** The original
  version blocked, and shutdown closed the listening socket to break it out. That works on
  Winsock and **does not work on Linux**, where a thread already blocked in `accept()` holds
  its own reference to the socket and never wakes — so shutdown hung forever, and the whole
  Linux CI suite hung with it. `shutdown()` before `close()` would have fixed it on Linux,
  but POSIX does not define `shutdown()` on a listening socket, and the bug was *caused* by
  relying on unspecified socket-closing side effects; swapping one for another would leave
  the same class of defect. Polling depends on nothing platform-specific. `select()` rather
  than `poll()`/`WSAPoll()` because it is spelled identically on both platforms.
- **`stop()` joins the accept thread before closing the listener**, so no socket is closed
  while another thread is still selecting on it.
- **Connection threads are still woken by `shutdown()` on their sockets**, which is
  well-defined on both platforms: a blocked `recv()` returns 0. Only the listening socket
  needed the polling treatment.
- **A comment asserting platform behaviour is a claim needing a platform to verify it.** The
  original blocking-accept comment explained its mechanism precisely and confidently, and was
  false on half the supported platforms — which almost certainly stopped anyone re-examining
  it. Treat such comments like benchmark numbers: they need a machine behind them.

### Build and tooling

- **Test target split from the main binary.** `engine_lib` holds all logic; `engine` is just
  `main.cpp` linked against it, so `tests/` can link the library without a second `main()`
  colliding with GoogleTest's.
- **`Release` is the default build type.** CMake's default is empty, meaning no optimisation
  flags — which silently invalidated every performance number produced before it was
  noticed, understating throughput by 2.2x.
- **Warnings are applied per target, never globally**, because GoogleTest and Google
  Benchmark are built from source here and must not be held to `-Werror`. All first-party
  targets route through one function so none is forgotten.
- **`-Werror` is opt-in locally and on in CI.** A newer compiler inventing a warning should
  not stop a developer building; in CI it is the whole point.
- **`-Wshadow=local` rather than `-Wshadow`.** Plain `-Wshadow` only flags the constructor
  parameter/member idiom here, which is safe in a mem-initializer list.
- **clang-tidy is advisory.** Its baseline has never been measured, and failing a build on an
  unmeasured baseline only means scrambling.

### Measurement

- **Quote p50/p95/p99, never `max`.** On this machine (4 cores @ 1190 MHz) `max` ranges from
  10 µs to 1172 µs run to run and measures OS scheduling, not the engine. Mean latency shows
  ~2x run-to-run variance, so single-run comparisons are not evidence — one conclusion about
  logging cost was drawn from a single run and later retracted when five repeats did not
  support it.
- **A/B comparisons must happen in the same session.** A baseline from a previous session is
  worthless here.
- **Benchmark artefacts go to `%TEMP%`, never the repo.** It sits in a OneDrive-synced
  folder, and the sync client inflated one measurement 4.5x by touching files mid-run.
- **For a first-touch race, iterate the process, not the loop counter.** 200 rounds inside
  one warm process found nothing where 300 one-round processes found 9 failures.

## Known gaps

Tracked in the README's [Known limitations](../README.md#known-limitations) section, which
is kept current. The largest by far is the unresolved `shared_mutex` shard-lookup bug, which
also blocks the next throughput ceiling.
