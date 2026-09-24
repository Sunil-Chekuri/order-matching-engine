#include "persistence/write_ahead_log.h"

#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
    // Prices must survive the round trip through text exactly, so they
    // are written with enough significant digits to reproduce the same
    // double. std::to_string would silently truncate to six decimals.
    std::string encodePrice(double price)
    {
        std::ostringstream out;

        out << std::setprecision(std::numeric_limits<double>::max_digits10)
            << price;

        return out.str();
    }

    bool splitFields(
        const std::string &line,
        std::size_t expected,
        std::vector<std::string> &out)
    {
        out.clear();

        std::string field;
        std::istringstream in(line);

        // The symbol is the final field and is taken verbatim, so a
        // symbol containing a comma would corrupt the record. Ticker
        // symbols do not contain commas; anything richer would need
        // quoting here.
        while (out.size() + 1 < expected && std::getline(in, field, ','))
            out.push_back(field);

        if (out.size() + 1 != expected)
            return false;

        if (!std::getline(in, field))
            return false;

        out.push_back(field);

        return true;
    }
}

void WriteAheadLog::open(
    const std::string &path)
{
    std::lock_guard<std::mutex>
        lock(mutex);

    file.open(path, std::ios::app);
}

bool WriteAheadLog::isOpen() const
{
    std::lock_guard<std::mutex>
        lock(mutex);

    return file.is_open();
}

void WriteAheadLog::appendSubmit(
    const Order &order)
{
    std::lock_guard<std::mutex>
        lock(mutex);

    if (!file.is_open())
        return;

    file << "S,"
         << order.order_id << ','
         << encodePrice(order.price) << ','
         << order.quantity << ','
         << static_cast<int>(order.side) << ','
         << static_cast<int>(order.type) << ','
         << order.participant_id << ','
         << order.symbol
         << std::endl;
}

void WriteAheadLog::appendCancel(
    int order_id,
    const std::string &symbol)
{
    std::lock_guard<std::mutex>
        lock(mutex);

    if (!file.is_open())
        return;

    file << "C,"
         << order_id << ','
         << symbol
         << std::endl;
}

void WriteAheadLog::close()
{
    std::lock_guard<std::mutex>
        lock(mutex);

    if (file.is_open())
        file.close();
}

std::vector<WalRecord> WriteAheadLog::readAll(
    const std::string &path)
{
    std::vector<WalRecord> records;

    std::ifstream in(path);

    if (!in.is_open())
        return records;

    std::string line;
    std::vector<std::string> fields;

    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        try
        {
            if (line[0] == 'S')
            {
                if (!splitFields(line, 8, fields))
                    continue;

                WalRecord record;
                record.kind = WalRecord::Kind::SUBMIT;
                record.order_id = std::stoi(fields[1]);
                record.price = std::stod(fields[2]);
                record.quantity = std::stoi(fields[3]);
                record.side = static_cast<Side>(std::stoi(fields[4]));
                record.type = static_cast<OrderType>(std::stoi(fields[5]));
                record.participant_id = std::stoi(fields[6]);
                record.symbol = fields[7];

                records.push_back(record);
            }
            else if (line[0] == 'C')
            {
                if (!splitFields(line, 3, fields))
                    continue;

                WalRecord record;
                record.kind = WalRecord::Kind::CANCEL;
                record.order_id = std::stoi(fields[1]);
                record.symbol = fields[2];

                records.push_back(record);
            }
        }
        catch (const std::exception &)
        {
            // A half-written final line is what a crash looks like.
            // Skip it and keep whatever was durably recorded before it.
            continue;
        }
    }

    return records;
}
