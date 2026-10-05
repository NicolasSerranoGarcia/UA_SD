#include "../units.hpp"

#include "../valve/valve.hpp"

namespace internal {

struct measureResult{
    units::liters_min currentFlux;
    units::liters_min expectedFlux;
};

class FlowMeter{
    public:
        static measureResult measure(const Valve& valve) noexcept{
            return measureResult{ .currentFlux = valve.flux_, 
                                    .expectedFlux = valve.requestedFlux_};
        };
};

} // namespace internal
