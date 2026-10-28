#pragma once

#include "engine/units.hpp"

#include "engine/valve/valve.hpp"
#include <algorithm>
#include <cmath>

namespace internal {

class FlowMeter {
public:
    static units::liters_min measure(const Valve &valve, double elapsedSeconds = 0) noexcept {
        const double time = std::max(0.0, elapsedSeconds);
        // Small pressure changes keep flow within 2% below the valve's setting.
        const double pressure = averagePressure + slowVariation * std::cos(time) +
                                fastVariation * std::cos(fastFrequency * time);
        return static_cast<units::liters_min>(valve.flux_ * pressure);
    }

    /// Integrate the varying flow for an unchanged valve setting.
    static double volume(const Valve &valve, double elapsedSeconds) noexcept {
        const double time = std::max(0.0, elapsedSeconds);
        return valve.flux_ * (averagePressure * time + slowVariation * std::sin(time) +
                             fastVariation * std::sin(fastFrequency * time) / fastFrequency) / 60;
    }

private:
    static constexpr double slowVariation = 0.006;
    static constexpr double fastVariation = 0.004;
    static constexpr double fastFrequency = 2.7;
    static constexpr double averagePressure = 1 - slowVariation - fastVariation;
};

} // namespace internal
