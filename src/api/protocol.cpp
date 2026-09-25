#include "api/protocol.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
    ProtocolReply ok(
        const std::string &body)
    {
        ProtocolReply reply;
        reply.line = body.empty() ? "OK" : ("OK " + body);
        return reply;
    }

    ProtocolReply err(
        const std::string &reason)
    {
        ProtocolReply reply;
        reply.line = "ERR " + reason;
        return reply;
    }

    std::vector<std::string> tokenise(
        const std::string &line)
    {
        std::vector<std::string> tokens;
        std::istringstream in(line);
        std::string token;

        // Splitting on arbitrary whitespace means a trailing carriage
        // return from a CRLF client is dropped for free, which is worth
        // having: telnet and most Windows clients send CRLF, and a
        // symbol of "DEFAULT\r" would silently become a second book.
        while (in >> token)
            tokens.push_back(token);

        return tokens;
    }

    std::string upper(
        std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        return value;
    }

    // std::stoi and std::stod stop at the first character they cannot
    // use, so "12abc" parses as 12 and "1.5" parses as 1 — both of which
    // would silently accept a malformed request. Requiring the whole
    // token to be consumed is what makes these a validation step rather
    // than a best-effort guess.
    bool parseInt(
        const std::string &token,
        int &out)
    {
        try
        {
            std::size_t consumed = 0;
            int value = std::stoi(token, &consumed);

            if (consumed != token.size())
                return false;

            out = value;
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    bool parseDouble(
        const std::string &token,
        double &out)
    {
        try
        {
            std::size_t consumed = 0;
            double value = std::stod(token, &consumed);

            if (consumed != token.size())
                return false;

            out = value;
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    bool parseSide(
        const std::string &token,
        Side &out)
    {
        const std::string value = upper(token);

        if (value == "BUY")
        {
            out = Side::BUY;
            return true;
        }

        if (value == "SELL")
        {
            out = Side::SELL;
            return true;
        }

        return false;
    }

    bool parseType(
        const std::string &token,
        OrderType &out)
    {
        const std::string value = upper(token);

        if (value == "LIMIT")
        {
            out = OrderType::LIMIT;
            return true;
        }

        if (value == "MARKET")
        {
            out = OrderType::MARKET;
            return true;
        }

        if (value == "IOC")
        {
            out = OrderType::IOC;
            return true;
        }

        if (value == "FOK")
        {
            out = OrderType::FOK;
            return true;
        }

        return false;
    }

    ProtocolReply handleSubmit(
        OrderGateway &gateway,
        const std::vector<std::string> &tokens)
    {
        // SUBMIT <id> <side> <type> <price> <qty> [symbol] [participant]
        if (tokens.size() < 6 || tokens.size() > 8)
            return err("bad_arity");

        int order_id = 0;
        if (!parseInt(tokens[1], order_id))
            return err("bad_id");

        Side side = Side::BUY;
        if (!parseSide(tokens[2], side))
            return err("bad_side");

        OrderType type = OrderType::LIMIT;
        if (!parseType(tokens[3], type))
            return err("bad_type");

        double price = 0.0;
        if (!parseDouble(tokens[4], price))
            return err("bad_price");

        int quantity = 0;
        if (!parseInt(tokens[5], quantity))
            return err("bad_quantity");

        const std::string symbol =
            tokens.size() > 6 ? tokens[6] : DEFAULT_SYMBOL;

        int participant_id = 0;
        if (tokens.size() > 7 && !parseInt(tokens[7], participant_id))
            return err("bad_participant");

        try
        {
            // A MARKET order still carries a price the engine ignores.
            // Order's constructor rejects non-positive prices (Day 2),
            // so a market order has to name some positive placeholder
            // rather than 0 — stated here because it is the kind of
            // thing a client author would otherwise discover by getting
            // a rejection.
            gateway.submitOrder(
                Order(order_id, price, quantity, side, type,
                      participant_id, symbol));
        }
        catch (const std::invalid_argument &e)
        {
            // The engine's own validation, surfaced rather than
            // swallowed. Anything thrown here would otherwise unwind
            // into the connection thread and drop a client for what is
            // really just a bad request.
            return err(std::string("rejected ") + e.what());
        }

        // What happened to the order is worth reporting, and the gateway
        // can already answer it: anything still resting is unfilled.
        // A fully filled order, or a MARKET/IOC remainder the engine
        // discarded, is simply not there.
        int resting = 0;
        if (!gateway.getRemainingQuantity(order_id, resting, symbol))
            resting = 0;

        return ok(
            "submitted " + std::to_string(order_id) +
            " resting " + std::to_string(resting));
    }

    ProtocolReply handleCancel(
        OrderGateway &gateway,
        const std::vector<std::string> &tokens)
    {
        if (tokens.size() < 2 || tokens.size() > 3)
            return err("bad_arity");

        int order_id = 0;
        if (!parseInt(tokens[1], order_id))
            return err("bad_id");

        const std::string symbol =
            tokens.size() > 2 ? tokens[2] : DEFAULT_SYMBOL;

        if (!gateway.cancelOrder(order_id, symbol))
            return err("not_found");

        return ok("cancelled " + std::to_string(order_id));
    }

    ProtocolReply handleQuantity(
        OrderGateway &gateway,
        const std::vector<std::string> &tokens)
    {
        if (tokens.size() < 2 || tokens.size() > 3)
            return err("bad_arity");

        int order_id = 0;
        if (!parseInt(tokens[1], order_id))
            return err("bad_id");

        const std::string symbol =
            tokens.size() > 2 ? tokens[2] : DEFAULT_SYMBOL;

        int quantity = 0;
        if (!gateway.getRemainingQuantity(order_id, quantity, symbol))
            return err("not_found");

        return ok("qty " + std::to_string(quantity));
    }

    ProtocolReply handleSnapshot(
        OrderGateway &gateway,
        const std::vector<std::string> &tokens)
    {
        if (tokens.size() < 2 || tokens.size() > 3)
            return err("bad_arity");

        int depth = 0;
        if (!parseInt(tokens[1], depth))
            return err("bad_depth");

        // A negative depth would convert to an enormous size_t and ask
        // the book to walk every level it has.
        if (depth < 0)
            return err("bad_depth");

        const std::string symbol =
            tokens.size() > 2 ? tokens[2] : DEFAULT_SYMBOL;

        return ok(
            toJsonLine(
                gateway.snapshot(
                    static_cast<std::size_t>(depth),
                    symbol)));
    }
}

ProtocolReply handleCommand(
    OrderGateway &gateway,
    const std::string &line)
{
    const std::vector<std::string> tokens = tokenise(line);

    if (tokens.empty())
        return err("empty");

    const std::string verb = upper(tokens[0]);

    if (verb == "PING")
    {
        if (tokens.size() != 1)
            return err("bad_arity");

        return ok("pong");
    }

    if (verb == "SUBMIT")
        return handleSubmit(gateway, tokens);

    if (verb == "CANCEL")
        return handleCancel(gateway, tokens);

    if (verb == "QTY")
        return handleQuantity(gateway, tokens);

    if (verb == "SNAPSHOT")
        return handleSnapshot(gateway, tokens);

    if (verb == "METRICS")
    {
        if (tokens.size() != 1)
            return err("bad_arity");

        return ok(toJsonLine(gateway.metrics()));
    }

    if (verb == "QUIT")
    {
        if (tokens.size() != 1)
            return err("bad_arity");

        ProtocolReply reply = ok("bye");
        reply.close_connection = true;
        return reply;
    }

    return err("unknown_command");
}
