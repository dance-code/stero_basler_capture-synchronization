#include "stopwatch.h"

#include <iostream>

void Stopwatch::Reset() {
    reset_time_ = last_split_time_ = std::chrono::high_resolution_clock::now();
}

void Stopwatch::Split() {
    auto now = std::chrono::high_resolution_clock::now();
    auto since_reset_time = now - reset_time_;
    auto since_last_split_time = now - last_split_time_;
    std::cout << "Time Since Reset: " << since_reset_time.count()
              << " ns ; Time Since Last Split: "
              << since_last_split_time.count() << " ns" << std::endl;
    last_split_time_ = now;
}