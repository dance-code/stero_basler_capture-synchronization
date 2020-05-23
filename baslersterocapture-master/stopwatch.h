#ifndef STOPWATCH_H_
#define STOPWATCH_H_

#include <chrono>

class Stopwatch {
public:
    Stopwatch() = default;
    ~Stopwatch() = default;

public:
    void Reset();
    void Split();

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> reset_time_;
    std::chrono::time_point<std::chrono::high_resolution_clock>
            last_split_time_;
};

#endif