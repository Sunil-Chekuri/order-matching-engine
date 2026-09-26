# Order Matching Engine (C++)

[![CI](https://github.com/Sunil-Chekuri/order-matching-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/Sunil-Chekuri/order-matching-engine/actions/workflows/ci.yml)

A high-performance, modular **Order Matching Engine** implemented in modern C++ using a production-style architecture.
This project simulates the core components of an exchange matching engine, including order processing, trade execution, latency measurement, and structured logging.

It is designed to demonstrate **systems programming**, **low-latency design**, and **clean software architecture**, similar to infrastructure used in trading and financial systems.

---

## Features

- Limit order matching (Buy/Sell)
- FIFO price-time priority
- Trade execution engine
- Order cancellation support
- Structured logging with timestamps
- Latency measurement
- Throughput calculation
- Modular architecture (API / Engine / Core / Utils)
- Modern C++ (C++17)
- CMake + Ninja build system
- Thread-safe logging

---

## Project Structure

```
order-matching-engine
│
├── src
│   ├── main.cpp
│
│   ├── core
│   │   ├── order.cpp
│   │   ├── trade.cpp
│
│   ├── engine
│   │   ├── matching_engine.cpp
│   │   ├── order_book.cpp
│
│   ├── api
│   │   ├── order_gateway.cpp
│
│   ├── utils
│   │   ├── logger.cpp
│   │   ├── timer.cpp
│
├── include
│   ├── core
│   │   ├── order.h
│   │   ├── trade.h
│
│   ├── engine
│   │   ├── matching_engine.h
│   │   ├── order_book.h
│
│   ├── api
│   │   ├── order_gateway.h
│
│   ├── utils
│   │   ├── logger.h
│   │   ├── timer.h
│
├── logs
│   └── engine.log
│
├── tests
│
├── CMakeLists.txt
└── README.md
```

---

## How the Engine Works

1. Orders are submitted through the `OrderGateway`
2. Orders are added to the `OrderBook`
3. The `MatchingEngine` checks for matches
4. Trades are executed using price-time priority
5. Metrics are recorded:
   - Total orders processed
   - Total trades executed
   - Latency
   - Throughput

6. Logs are written to a file

---

## Example Log Output

```
2026-05-01 21:30:01 [INFO] Engine started
2026-05-01 21:30:01 [INFO] Trades executed: 1000
2026-05-01 21:30:01 [INFO] Total orders processed: 20000
2026-05-01 21:30:01 [INFO] Total latency: 42000 us
2026-05-01 21:30:01 [INFO] Throughput: 476190 orders/sec
2026-05-01 21:30:01 [INFO] Engine shutdown
```

---

## Requirements

- C++17 or later
- GCC 11+ (currently tested with GCC 16)
- CMake
- Ninja (recommended)

---

## Build Instructions

From the project root:

```
Remove-Item build -Recurse -Force
mkdir build
cd build

cmake .. -G Ninja
cmake --build .
```

Run the engine:

```
.\engine
```

---

## Performance Metrics

The engine records:

- Total Orders Processed
- Total Trades Executed
- Latency (microseconds)
- Throughput (orders per second)
- Average latency per order

These metrics are commonly monitored in:

- Trading engines
- Matching systems
- Low-latency infrastructure
- Real-time systems

---

## Future Improvements

- Multithreaded order processing
- Lock-free data structures
- Risk management checks
- Market data feed simulation
- REST API interface
- Unit testing (GoogleTest)
- Benchmarking tools
- Order persistence
- Metrics dashboard

---

## Author

Sunil Kumar
B.Tech Information Technology — NITK
Associate Developer — TransUnion

---

## License

MIT License
