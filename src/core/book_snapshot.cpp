#include "core/book_snapshot.h"

#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
    // Same reasoning as the write-ahead log's encodePrice: a price has
    // to reproduce the identical double when read back, or a consumer
    // sees a level at a price the book does not actually hold.
    void writeLevels(
        std::ostringstream &out,
        const std::vector<PriceLevel> &levels)
    {
        out << '[';

        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            if (i > 0)
                out << ',';

            out << "{\"price\":"
                << std::setprecision(std::numeric_limits<double>::max_digits10)
                << levels[i].price
                << ",\"quantity\":" << levels[i].total_quantity
                << ",\"orders\":" << levels[i].order_count
                << '}';
        }

        out << ']';
    }
}

std::string toJsonLine(
    const BookSnapshot &snapshot)
{
    std::ostringstream out;

    out << "{\"bids\":";
    writeLevels(out, snapshot.bids);

    out << ",\"asks\":";
    writeLevels(out, snapshot.asks);

    out << '}';

    return out.str();
}
