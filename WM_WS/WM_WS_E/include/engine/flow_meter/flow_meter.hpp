#pragma once

#include "engine/units.hpp"

#include "engine/valve/valve.hpp"

namespace internal {

class FlowMeter{
    public:
        static units::liters_min measure(const Valve& valve) noexcept{
            return valve.flux_;
        };
};

} // namespace internal
