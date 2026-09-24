#include "core/order.h"

#include <stdexcept>

Order::Order(
    int id,
    double p,
    int qty,
    Side s,
    OrderType t,
    int participant_id,
    const std::string &symbol)
    : order_id(id),
      price(p),
      quantity(qty),
      side(s),
      type(t),
      participant_id(participant_id),
      symbol(symbol),
      timestamp(
          std::chrono::high_resolution_clock::now())
{
    if (p <= 0.0)
        throw std::invalid_argument("Order price must be positive");

    if (qty <= 0)
        throw std::invalid_argument("Order quantity must be positive");
}