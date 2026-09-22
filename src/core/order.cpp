#include "core/order.h"

#include <stdexcept>

Order::Order(
    int id,
    double p,
    int qty,
    Side s,
    OrderType t)
    : order_id(id),
      price(p),
      quantity(qty),
      side(s),
      type(t),
      timestamp(
          std::chrono::high_resolution_clock::now())
{
    if (p <= 0.0)
        throw std::invalid_argument("Order price must be positive");

    if (qty <= 0)
        throw std::invalid_argument("Order quantity must be positive");
}