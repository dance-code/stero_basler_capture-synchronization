#ifndef RATE_H_
#define RATE_H_

#include <chrono>

class Rate {
public:
    Rate(double frequency = 10.0);
    ~Rate() = default;

    double GetRate() const;
    void SetRate(double frequency);

    void Init();
    void Sleep();

private:
    double frequency_;
    std::chrono::nanoseconds duration_;
    std::chrono::time_point<std::chrono::high_resolution_clock>
            last_sleep_time_;
};

#endif RATE_H_