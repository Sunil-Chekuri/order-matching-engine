#include "cli/depth_view.h"

#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace
{
    const char *OK_PREFIX = "OK ";
    const char *ERR_PREFIX = "ERR ";

    // Strips the status word and hands back the payload, or reports the
    // server's own error reason so the caller can print it verbatim
    // rather than inventing its own wording.
    bool takePayload(
        const std::string &reply,
        std::string &payload,
        std::string &error)
    {
        if (reply.rfind(ERR_PREFIX, 0) == 0)
        {
            error = reply.substr(std::string(ERR_PREFIX).size());
            return false;
        }

        if (reply.rfind(OK_PREFIX, 0) != 0)
        {
            error = "unexpected reply: " + reply;
            return false;
        }

        payload = reply.substr(std::string(OK_PREFIX).size());
        return true;
    }

    // Finds "<key>": inside [from, to) and parses the number after it.
    // Returns the position just past the number, or npos on failure.
    std::size_t readNumber(
        const std::string &text,
        const std::string &key,
        std::size_t from,
        std::size_t to,
        double &out)
    {
        const std::string needle = "\"" + key + "\":";

        const std::size_t at = text.find(needle, from);

        if (at == std::string::npos || at >= to)
            return std::string::npos;

        const std::size_t start = at + needle.size();

        try
        {
            std::size_t consumed = 0;
            out = std::stod(text.substr(start, to - start), &consumed);

            if (consumed == 0)
                return std::string::npos;

            return start + consumed;
        }
        catch (const std::exception &)
        {
            return std::string::npos;
        }
    }

    // Reads the array that follows "<key>":[ and collects one PriceLevel
    // per {...} object inside it.
    bool readLevels(
        const std::string &text,
        const std::string &key,
        std::vector<PriceLevel> &out,
        std::string &error)
    {
        out.clear();

        const std::string needle = "\"" + key + "\":[";

        const std::size_t at = text.find(needle);

        if (at == std::string::npos)
        {
            error = "missing \"" + key + "\" array";
            return false;
        }

        const std::size_t open = at + needle.size();
        const std::size_t close = text.find(']', open);

        if (close == std::string::npos)
        {
            error = "unterminated \"" + key + "\" array";
            return false;
        }

        std::size_t cursor = open;

        while (true)
        {
            const std::size_t brace = text.find('{', cursor);

            if (brace == std::string::npos || brace > close)
                break;

            const std::size_t end = text.find('}', brace);

            if (end == std::string::npos || end > close)
            {
                error = "unterminated level in \"" + key + "\"";
                return false;
            }

            double price = 0.0;
            double quantity = 0.0;
            double orders = 0.0;

            if (readNumber(text, "price", brace, end, price) == std::string::npos ||
                readNumber(text, "quantity", brace, end, quantity) == std::string::npos ||
                readNumber(text, "orders", brace, end, orders) == std::string::npos)
            {
                error = "malformed level in \"" + key + "\"";
                return false;
            }

            PriceLevel level;
            level.price = price;
            level.total_quantity = static_cast<int>(quantity);
            level.order_count = static_cast<int>(orders);

            out.push_back(level);

            cursor = end + 1;
        }

        return true;
    }

    // Natural rendering: enough precision to be honest, with trailing
    // zeros dropped. Used for scalars like the spread, which stand
    // alone rather than in a column.
    std::string formatPrice(
        double price)
    {
        std::ostringstream out;
        out << std::setprecision(10) << price;
        return out.str();
    }

    std::size_t decimalsIn(
        double price)
    {
        const std::string text = formatPrice(price);

        const std::size_t point = text.find('.');

        if (point == std::string::npos)
            return 0;

        return text.size() - point - 1;
    }

    // Every price in the ladder is rendered with the same number of
    // decimals, chosen as the most any one of them needs. Rendering
    // each at its own natural precision leaves the column ragged
    // (100.5 above 100.75 above 101), which is exactly the reading a
    // depth ladder exists to make easy -- the decimal points have to
    // line up. Capped so a price carrying float noise cannot stretch
    // the column indefinitely.
    std::size_t ladderDecimals(
        const BookSnapshot &snapshot)
    {
        const std::size_t cap = 8;

        std::size_t needed = 0;

        for (const PriceLevel &level : snapshot.bids)
            needed = needed > decimalsIn(level.price) ? needed : decimalsIn(level.price);

        for (const PriceLevel &level : snapshot.asks)
            needed = needed > decimalsIn(level.price) ? needed : decimalsIn(level.price);

        return needed > cap ? cap : needed;
    }

    std::string formatLadderPrice(
        double price,
        std::size_t decimals)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(static_cast<int>(decimals)) << price;
        return out.str();
    }

    std::string padLeft(
        const std::string &value,
        std::size_t width)
    {
        if (value.size() >= width)
            return value;

        return std::string(width - value.size(), ' ') + value;
    }

    const std::size_t COUNT_WIDTH = 6;
    const std::size_t QTY_WIDTH = 9;
    const std::size_t PRICE_WIDTH = 12;
}

bool parseSnapshotReply(
    const std::string &reply,
    BookSnapshot &out,
    std::string &error)
{
    error.clear();

    std::string payload;

    if (!takePayload(reply, payload, error))
        return false;

    BookSnapshot parsed;

    if (!readLevels(payload, "bids", parsed.bids, error))
        return false;

    if (!readLevels(payload, "asks", parsed.asks, error))
        return false;

    out = parsed;

    return true;
}

bool parseMetricsReply(
    const std::string &reply,
    EngineMetrics &out,
    std::string &error)
{
    error.clear();

    std::string payload;

    if (!takePayload(reply, payload, error))
        return false;

    struct Field
    {
        const char *name;
        long long *target;
    };

    EngineMetrics parsed;

    long long symbols = 0;

    const Field fields[] = {
        {"orders_submitted", &parsed.orders_submitted},
        {"trades_executed", &parsed.trades_executed},
        {"cancels_accepted", &parsed.cancels_accepted},
        {"cancels_rejected", &parsed.cancels_rejected},
        {"resting_orders", &parsed.resting_orders},
        {"symbols", &symbols},
    };

    for (const Field &field : fields)
    {
        double value = 0.0;

        if (readNumber(payload, field.name, 0, payload.size(), value) ==
            std::string::npos)
        {
            error = std::string("missing \"") + field.name + "\"";
            return false;
        }

        *field.target = static_cast<long long>(value);
    }

    parsed.symbols = static_cast<std::size_t>(symbols);

    out = parsed;

    return true;
}

std::string renderDepth(
    const std::string &symbol,
    const BookSnapshot &snapshot)
{
    std::ostringstream out;

    out << symbol
        << "   bids " << snapshot.bids.size()
        << "  asks " << snapshot.asks.size()
        << "\n\n";

    out << padLeft("count", COUNT_WIDTH)
        << padLeft("qty", QTY_WIDTH)
        << padLeft("bid", PRICE_WIDTH)
        << "  |  "
        << padLeft("ask", PRICE_WIDTH)
        << padLeft("qty", QTY_WIDTH)
        << padLeft("count", COUNT_WIDTH)
        << "\n";

    const std::size_t half =
        COUNT_WIDTH + QTY_WIDTH + PRICE_WIDTH;

    out << std::string(half, '-')
        << "  +  "
        << std::string(half, '-')
        << "\n";

    const std::size_t decimals = ladderDecimals(snapshot);

    const std::size_t rows =
        snapshot.bids.size() > snapshot.asks.size()
            ? snapshot.bids.size()
            : snapshot.asks.size();

    if (rows == 0)
    {
        out << padLeft("(empty)", half)
            << "  |  "
            << "\n";
    }

    for (std::size_t i = 0; i < rows; ++i)
    {
        if (i < snapshot.bids.size())
        {
            out << padLeft(std::to_string(snapshot.bids[i].order_count), COUNT_WIDTH)
                << padLeft(std::to_string(snapshot.bids[i].total_quantity), QTY_WIDTH)
                << padLeft(formatLadderPrice(snapshot.bids[i].price, decimals), PRICE_WIDTH);
        }
        else
        {
            out << std::string(half, ' ');
        }

        out << "  |  ";

        if (i < snapshot.asks.size())
        {
            out << padLeft(formatLadderPrice(snapshot.asks[i].price, decimals), PRICE_WIDTH)
                << padLeft(std::to_string(snapshot.asks[i].total_quantity), QTY_WIDTH)
                << padLeft(std::to_string(snapshot.asks[i].order_count), COUNT_WIDTH);
        }

        out << "\n";
    }

    // A spread only exists when both sides do. Printing one against an
    // empty side would be inventing a number.
    if (!snapshot.bids.empty() && !snapshot.asks.empty())
    {
        const double best_bid = snapshot.bids.front().price;
        const double best_ask = snapshot.asks.front().price;

        out << "\nspread " << formatPrice(best_ask - best_bid)
            << "   mid " << formatPrice((best_ask + best_bid) / 2.0)
            << "\n";
    }

    return out.str();
}

std::string renderMetrics(
    const EngineMetrics &metrics)
{
    std::ostringstream out;

    out << "orders " << metrics.orders_submitted
        << "   trades " << metrics.trades_executed
        << "   resting " << metrics.resting_orders
        << "   cancels " << metrics.cancels_accepted
        << "/" << metrics.cancels_rejected
        << "   symbols " << metrics.symbols;

    return out.str();
}
