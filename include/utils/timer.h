#pragma once

#include <chrono>

class Timer
{
private:

    std::chrono::high_resolution_clock::time_point start_time;

public:

    void start();

    long long stop();
};