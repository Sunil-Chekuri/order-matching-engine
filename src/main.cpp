#include "api/order_gateway.h"
#include "utils/timer.h"
#include "utils/logger.h"

int main()
{
    Logger::init();

    Logger::log(
        LogLevel::INFO,
        "Engine started");

    OrderGateway gateway;

    Timer timer;

    const int NUM_ORDERS = 10000;

    int total_orders =
        NUM_ORDERS * 2;

    timer.start();

    for (int i = 0; i < NUM_ORDERS; ++i)
    {
        gateway.submitOrder(
            Order(
                i,
                100.0,
                10,
                Side::BUY));

        gateway.submitOrder(
            Order(
                i + NUM_ORDERS,
                100.0,
                10,
                Side::SELL));
    }

    auto latency =
        timer.stop();

    Logger::log(
        LogLevel::INFO,
        "Total orders processed: " + std::to_string(total_orders));

    Logger::log(
        LogLevel::INFO,
        "Total latency: " + std::to_string(latency) + " us");

    double throughput =
        total_orders /
        (latency / 1e6);

    Logger::log(
        LogLevel::INFO,
        "Throughput: " + std::to_string(throughput) + " orders/sec");

    double avg_latency =
        latency /
        total_orders;

    Logger::log(
        LogLevel::INFO,
        "Average latency per order: " + std::to_string(avg_latency) + " us");

    Logger::log(
        LogLevel::INFO,
        "Engine shutdown");

    Logger::shutdown();

    return 0;
}