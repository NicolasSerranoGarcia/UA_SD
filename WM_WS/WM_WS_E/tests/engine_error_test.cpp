#include "../include/engine/engine.hpp"
#include <cassert>
#include <limits>
#include <type_traits>

static_assert(std::is_same_v<decltype(Engine{}.start()), std::expected<void, Engine::Error>>);

int main() {
    Engine engine;
    for (float invalid : {-1.f, 91.f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        const auto result = engine.start(invalid);
        assert(!result);
        assert(result.error().type == Engine::ErrorType::InvalidFlow);
        assert(result.error().invalidFlow);
        assert(result.error().invalidFlow->maximum == internal::Valve::maxFlux);
        const auto requested = result.error().invalidFlow->requested;
        assert(std::isnan(invalid) ? std::isnan(requested) : requested == invalid);
    }
    // Rejected inputs did not transition the engine to Watering.
    assert(engine.start(40));
    auto repeated = engine.start(40);
    assert(!repeated && repeated.error().type == Engine::ErrorType::AlreadyWatering);
    assert(!repeated.error().invalidFlow);

    assert(engine.stop());
    assert(engine.measure().currentFlux == 0);
    assert(engine.start()); // Normal stop does not block future irrigation.
    engine.injectFault();
    assert(engine.measure().currentFlux == 0);
    assert(engine.measure().expectedFlux == 0);
    auto health = engine.healthCheck();
    assert(!health && health.error().type == Engine::ErrorType::Fault);
    auto faultedStart = engine.start();
    assert(!faultedStart && faultedStart.error().type == Engine::ErrorType::Fault);
    assert(engine.stop());
    assert(!engine.healthCheck()); // Stopping does not repair a fault.
    engine.repair();
    assert(engine.healthCheck());
    assert(engine.measure().currentFlux == 0);
    assert(engine.start());

    assert(engine.block(0));
    assert(engine.measure().currentFlux == 0);
    auto blockedStart = engine.start();
    assert(!blockedStart && blockedStart.error().type == Engine::ErrorType::BlockedByCentral);
    engine.injectFault();
    assert(engine.unblock(0));
    assert(!engine.healthCheck()); // Unblocking does not repair a fault.
    assert(!engine.start());
    assert(engine.block(0));
    engine.repair();
    assert(engine.healthCheck());
    blockedStart = engine.start();
    assert(!blockedStart && blockedStart.error().type == Engine::ErrorType::BlockedByCentral);
    assert(engine.unblock(0));
    assert(engine.measure().currentFlux == 0); // Unblocking does not start irrigation.
    assert(engine.start());
    assert(engine.stop());
    auto stopped = engine.stop();
    assert(!stopped && stopped.error().type == Engine::ErrorType::AlreadyStopped);
    engine.injectFault(); // Faults can also be injected while idle.
    assert(!engine.healthCheck());
    assert(!engine.start());
    engine.repair();
    assert(engine.start());

    Engine configured(Engine::Config{.irrigationTimeoutLimit = 2min});
    assert(configured.start());
}
