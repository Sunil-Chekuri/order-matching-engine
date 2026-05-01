#pragma once

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel
{
    INFO,
    WARNING,
    ERROR,
    DEBUG
};

class Logger
{
private:
    static std::ofstream log_file;

    static std::mutex log_mutex;

    static std::string currentTime();

    static std::string levelToString(
        LogLevel level);

public:
    static void init();

    static void log(
        LogLevel level,
        const std::string &message);

    static void shutdown();
};