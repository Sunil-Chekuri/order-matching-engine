#pragma once

#include <string>
#include <fstream>
#include <mutex>

// Ordered by severity so that filtering is a comparison. DEBUG was
// originally declared last, which made "at least this severe" impossible
// to express; nothing persists these values, so reordering is safe.
enum class LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger
{
private:
    static std::ofstream log_file;

    static std::mutex log_mutex;

    static bool enabled;

    static LogLevel min_level;

    static std::string currentTime();

    static std::string levelToString(
        LogLevel level);

public:
    static void init();

    static void setEnabled(
        bool value);

    // Messages below this severity are discarded before any formatting
    // or I/O happens. Defaults to INFO, so DEBUG costs a comparison.
    static void setMinLevel(
        LogLevel level);

    static void log(
        LogLevel level,
        const std::string &message);

    static void shutdown();
};