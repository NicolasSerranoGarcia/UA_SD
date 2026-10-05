#include "../include/valve/valve.hpp"
#include "../include/flow_meter/flow_meter.hpp"
#include <cassert>
#include <limits>

int main() {
    internal::Valve healthy;
    assert(healthy.openBy(20));
    assert(internal::FlowMeter::measure(healthy) == 20);
    assert(healthy.closeBy(5));
    assert(internal::FlowMeter::measure(healthy) == 15);
    for (float bad : {-1.f, 91.f, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
        auto result = healthy.openBy(bad);
        assert(!result && result.error().type == internal::Valve::ErrorType::InvalidFlow);
        assert(!healthy.closeBy(bad));
        assert(internal::FlowMeter::measure(healthy) == 15);
        bool threw = false;
        try { internal::Valve invalid(bad); } catch (const std::invalid_argument&) { threw = true; }
        assert(threw);
    }
    assert(!healthy.closeBy(16));
    assert(!healthy.openBy(76));
    healthy.openFully();
    assert(internal::FlowMeter::measure(healthy) == 90);
    healthy.close();
    assert(internal::FlowMeter::measure(healthy) == 0);

    // Repeated use no longer injects physical faults.
    for (int i = 0; i < 100; ++i) {
        healthy.openFully();
        assert(internal::FlowMeter::measure(healthy) == internal::Valve::maxFlux);
        healthy.close();
        assert(internal::FlowMeter::measure(healthy) == 0);
        healthy.close();
        assert(internal::FlowMeter::measure(healthy) == 0);
    }
}
