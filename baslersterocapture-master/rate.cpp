#include "rate.h"

#include <iostream>
#include <thread>

Rate::Rate(double frequency) {
    SetRate(frequency);
}

double Rate::GetRate() const {
    return frequency_;
}

void Rate::SetRate(double frequency) {
    frequency_ = frequency;
    duration_ = std::chrono::nanoseconds(
            static_cast<int64_t>(1000000000.0 / frequency));
}

void Rate::Init() {
    last_sleep_time_ = std::chrono::high_resolution_clock::now();
}

void Rate::Sleep() {
    auto now = std::chrono::high_resolution_clock::now();
    auto diff = now - last_sleep_time_;

    auto sleep_ns = duration_.count() - diff.count();

    if (sleep_ns > 0) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
    } else {
        std::cout << "Rate Timeout : " << -sleep_ns << " ns" << std::endl;
    }

    last_sleep_time_ = std::chrono::high_resolution_clock::now();
}