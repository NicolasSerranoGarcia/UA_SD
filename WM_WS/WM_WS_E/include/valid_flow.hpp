#pragma once

#include "units.hpp"
#include <cmath>

namespace internal {

    inline bool validFlow(units::liters_min value, units::liters_min maximum) noexcept {
        return std::isfinite(value) && value >= 0 && value <= maximum;
    }

} // namespace internal
