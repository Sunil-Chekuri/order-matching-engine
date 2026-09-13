# Design Notes

Working reference for module ownership and decisions made while building this project out. Updated as the design evolves; not a polished README.

## Module ownership

- **core** (`Order`, `Trade`) — plain data types with no behavior beyond construction. No dependencies on other modules.
- **engine/OrderBook** — owns the resting order state for a single symbol: bids and asks sorted by price, FIFO within a price level (`std::map<price, std::deque<Order>>`), plus an `order_id -> Order*` registry for O(1) cancellation lookup. Knows nothing about matching logic, only book structure.
- **engine/MatchingEngine** — owns the matching loop: pulls best bid/ask from the book, generates `Trade`s, and updates/removes orders as they fill. Knows nothing about how orders arrive.
- **api/OrderGateway** — the external-facing entry point; currently a thin pass-through to `MatchingEngine`. This is the seam where a future network layer (Day 15) plugs in without touching engine internals.
- **utils** (`Logger`, `Timer`) — cross-cutting infrastructure, no dependency on domain types.

## Decisions so far

- **`std::map` for price levels, not a hash map**: price levels need ordered iteration (best bid = highest price, best ask = lowest price) every match attempt, which a hash map can't give without a full re-sort. `std::map` keeps `O(log n)` insert and `O(1)` access to the best level via `begin()`.
- **Bids use `std::greater<double>`, asks use default ascending order**: this makes `bids.begin()` and `asks.begin()` both mean "best price" without extra logic at the call site.
- **`order_registry` stores raw `Order*` into the deque**: avoids a linear scan through price levels on cancel, at the cost of needing to be careful that pointers stay valid — `std::deque` guarantees pointer stability on `push_back`/`pop_front` as long as we don't erase from the middle carelessly, which is why `cancelOrder` finds and erases in place rather than reallocating.
- **Test target split from the main binary**: `engine_lib` (a static library) holds all logic; `engine` (the executable) is just `main.cpp` linked against it. This lets `tests/` link `engine_lib` directly without a second definition of `main()` colliding with GoogleTest's own.
- **Build tooling note**: on this machine, invoking the MinGW toolchain (`cc1plus.exe` and friends) through the git-bash-based shell tool silently fails (exit 127, no error text) even with sandboxing disabled — but works fine through PowerShell. Any CMake configure/build/test steps for this project should be run via PowerShell, not the bash-style shell tool.

## Known gaps (tracked against the 3-week plan)

- Only a `MARKET`-less limit order type exists; `IOC`/`FOK`/`MARKET` are Week 1, Day 5.
- No self-trade prevention yet (Day 6).
- Single-threaded, single-symbol only (Week 2).
- No persistence/WAL (Day 12).
