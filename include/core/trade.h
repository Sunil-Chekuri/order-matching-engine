#pragma once

class Trade
{
public:
    int trade_id;

    int buy_order_id;

    int sell_order_id;

    double price;

    int quantity;

    Trade(
        int id,
        int buy_id,
        int sell_id,
        double p,
        int qty);
};