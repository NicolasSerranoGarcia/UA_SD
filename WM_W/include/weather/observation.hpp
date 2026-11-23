#pragma once

#include <chrono>

namespace weather {

struct Observation {
    double temperatureC;
    bool raining;
    std::chrono::sys_seconds observedAt;
    std::chrono::sys_seconds fetchedAt;

    bool restricted() const { return temperatureC < 0.0 || raining; }
};

} // namespace weather
