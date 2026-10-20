#include "../include/engine/engine.hpp"
#include <cassert>
#include <limits>
#include <type_traits>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

static_assert(std::is_same_v<decltype(Engine{}.start()), std::expected<void, Engine::Error>>);

int main() {
    Engine engine;
    assert(!engine.isRunning());
    assert(engine.timeToShutdown() == 0s);
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
    assert(engine.isRunning());
    assert(engine.timeToShutdown() > 0s);
    auto repeated = engine.start(40);
    assert(!repeated && repeated.error().type == Engine::ErrorType::AlreadyWatering);
    assert(!repeated.error().invalidFlow);

    assert(engine.stop());
    assert(!engine.isRunning());
    assert(engine.timeToShutdown() == 0s);
    assert(engine.measureFlow() == 0);
    assert(!engine.isRunning());
    assert(engine.start()); // Normal stop does not block future irrigation.
    engine.injectFault();
    assert(engine.measureFlow() == 0);
    assert(!engine.isRunning());
    auto health = engine.isHealthy();
    assert(!health);
    auto faultedStart = engine.start();
    assert(!faultedStart && faultedStart.error().type == Engine::ErrorType::Fault);
    assert(engine.stop());
    assert(!engine.isRunning());
    assert(engine.timeToShutdown() == 0s);
    assert(!engine.isHealthy()); // Stopping does not repair a fault.
    engine.repair();
    assert(engine.isHealthy());
    assert(engine.measureFlow() == 0);
    assert(!engine.isRunning());
    assert(engine.start());

    assert(engine.block(0));
    assert(engine.measureFlow() == 0);
    assert(!engine.isRunning());
    auto blockedStart = engine.start();
    assert(!blockedStart && blockedStart.error().type == Engine::ErrorType::BlockedByCentral);
    engine.injectFault();
    assert(engine.unblock(0));
    assert(!engine.isHealthy()); // Unblocking does not repair a fault.
    assert(!engine.start());
    assert(engine.block(0));
    engine.repair();
    assert(engine.isHealthy());
    blockedStart = engine.start();
    assert(!blockedStart && blockedStart.error().type == Engine::ErrorType::BlockedByCentral);
    assert(engine.unblock(0));
    assert(engine.measureFlow() == 0);
    assert(!engine.isRunning()); // Unblocking does not start irrigation.
    assert(engine.start());
    assert(engine.stop());
    assert(!engine.isRunning());
    assert(engine.timeToShutdown() == 0s);
    auto stopped = engine.stop();
    assert(!stopped && stopped.error().type == Engine::ErrorType::AlreadyStopped);
    engine.injectFault(); // Faults can also be injected while idle.
    assert(!engine.isHealthy());
    assert(!engine.start());
    engine.repair();
    assert(engine.start());

    Engine configured(Engine::Config{.irrigationTimeoutLimit = 120});
    assert(configured.start());
}
