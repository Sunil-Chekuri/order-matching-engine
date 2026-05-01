#include "utils/logger.h"

#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

std::ofstream Logger::log_file;

std::mutex Logger::log_mutex;

void Logger::init()
{
    log_file.open(
        "give log file location here",
        std::ios::app);

    if (!log_file.is_open())
    {
        std::cerr
            << "Failed to open log file"
            << std::endl;
    }
}

std::string Logger::levelToString(
    LogLevel level
)
{
    switch (level)
    {
        case LogLevel::INFO:
            return "INFO";

        case LogLevel::WARNING:
            return "WARN";

        case LogLevel::ERROR:
            return "ERROR";

        case LogLevel::DEBUG:
            return "DEBUG";

        default:
            return "UNKNOWN";
    }
}

std::string Logger::currentTime()
{
    auto now =
        std::chrono::system_clock::now();

    auto time =
        std::chrono::system_clock::to_time_t(
            now);

    std::tm tm;

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;

    oss
        << std::put_time(
               &tm,
               "%Y-%m-%d %H:%M:%S");

    return oss.str();
}

void Logger::log(
    LogLevel level,
    const std::string& message
)
{
    std::lock_guard<std::mutex>
        lock(log_mutex);

    if (log_file.is_open())
    {
        log_file
            << currentTime()
            << " ["
            << levelToString(level)
            << "] "
            << message
            << std::endl;
    }
}

void Logger::shutdown()
{
    if (log_file.is_open())
        log_file.close();
}
