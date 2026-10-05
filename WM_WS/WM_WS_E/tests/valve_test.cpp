#include "../include/valve/valve.hpp"
#include <cassert>
#include <limits>

int main() {
    internal::Valve healthy;
    assert(healthy.openBy(20));
    assert(healthy.actualFlow() == 20);
    assert(healthy.closeBy(5));
    assert(healthy.actualFlow() == 15);
    for (float bad : {-1.f, 91.f, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
        auto result = healthy.openBy(bad);
        assert(!result && result.error().type == internal::Valve::ErrorType::InvalidFlow);
        assert(!healthy.closeBy(bad));
        assert(healthy.actualFlow() == 15);
        bool threw = false;
        try { internal::Valve invalid(bad); } catch (const std::invalid_argument&) { threw = true; }
        assert(threw);
    }
    assert(!healthy.closeBy(16));
    assert(!healthy.openBy(76));
    healthy.openFully();
    assert(healthy.actualFlow() == 90);
    healthy.close();
    assert(healthy.actualFlow() == 0);

    // Repeated use no longer injects physical faults.
    for (int i = 0; i < 100; ++i) {
        healthy.openFully();
        assert(healthy.actualFlow() == internal::Valve::maxFlux);
        healthy.close();
        assert(healthy.actualFlow() == 0);
        healthy.close();
        assert(healthy.actualFlow() == 0);
    }
}
