#include "order.h"

Order::Order(
    int id,
    double p,
    int qty,
    Side s)
    : order_id(id),
      price(p),
      quantity(qty),
      side(s),
      timestamp(
          std::chrono::high_resolution_clock::now())
{
}
