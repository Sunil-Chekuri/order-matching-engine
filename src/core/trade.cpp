#include "core/trade.h"

#include <stdexcept>

Trade::Trade(
    int id,
    int buy_id,
    int sell_id,
    double p,
    int qty)
    : trade_id(id),
      buy_order_id(buy_id),
      sell_order_id(sell_id),
      price(p),
      quantity(qty)
{
    if (p <= 0.0)
        throw std::invalid_argument("Trade price must be positive");

    if (qty <= 0)
        throw std::invalid_argument("Trade quantity must be positive");
}