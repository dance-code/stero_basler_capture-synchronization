#include "utils.h"

#include <sstream>

#include "date.h"
//#include "tz.h"

std::string TimeStr() {
    using namespace date;
    using namespace std::chrono;
    std::ostringstream oss;
    auto t = system_clock::now();
    t += hours(8);
    // auto t_local = make_zoned(current_zone()->name(), t);
    oss << "[" << t << "] ";
    return oss.str();
}

std::string TimeStrLocal() {
    time_t t = time(0);
    char tmp[64];
    struct tm buf;
    localtime_s(&buf, &t);
    strftime(tmp, sizeof(tmp), "%Y-%m-%d-%H-%M-%S", &buf);
    return "[" + std::string(tmp) + "]";
}