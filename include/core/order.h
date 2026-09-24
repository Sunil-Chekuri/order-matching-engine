#pragma once

#include <chrono>
#include <string>

enum class Side
{
    BUY,
    SELL
};

enum class OrderType
{
    LIMIT,
    MARKET,
    IOC,
    FOK
};

// Used wherever a symbol is not stated explicitly, so single-instrument
// callers can ignore symbols entirely and still land on one consistent
// book.
inline const std::string DEFAULT_SYMBOL = "DEFAULT";

class Order
{
public:
    int order_id;
    double price;
    int quantity;
    Side side;
    OrderType type;

    // 0 means "no participant specified" and never triggers self-trade
    // prevention, even against another order that also defaults to 0.
    int participant_id;

    // The instrument this order trades. Orders only ever match against
    // other orders carrying the same symbol — the engine keeps a
    // separate book per symbol.
    //
    // A real design would put this first, since it is the most
    // fundamental field here; it trails only because adding it as a
    // defaulted parameter kept every existing call site compiling.
    std::string symbol;

    std::chrono::high_resolution_clock::time_point timestamp;

    Order(
        int id,
        double p,
        int qty,
        Side s,
        OrderType t = OrderType::LIMIT,
        int participant_id = 0,
        const std::string &symbol = DEFAULT_SYMBOL);
};