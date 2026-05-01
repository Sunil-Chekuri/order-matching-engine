#include "core/trade.h"

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
}