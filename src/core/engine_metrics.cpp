#include "core/engine_metrics.h"

#include <sstream>

std::string toJsonLine(
    const EngineMetrics &metrics)
{
    std::ostringstream out;

    out << '{'
        << "\"orders_submitted\":" << metrics.orders_submitted << ','
        << "\"trades_executed\":" << metrics.trades_executed << ','
        << "\"cancels_accepted\":" << metrics.cancels_accepted << ','
        << "\"cancels_rejected\":" << metrics.cancels_rejected << ','
        << "\"resting_orders\":" << metrics.resting_orders << ','
        << "\"symbols\":" << metrics.symbols
        << '}';

    return out.str();
}
