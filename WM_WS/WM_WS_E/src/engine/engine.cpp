#include "engine/engine.hpp"

#include "engine/flow_meter/flow_meter.hpp"
#include "engine/leak_detector/leak_detector.hpp"
#include "engine/valid_flow.hpp"

#include <cassert>
#include <exception>
#include <list>
#include <stop_token>
#include <thread>

// Registry lock precedes an engine lock. The worker is destroyed before its data.
struct Engine::Registry {
    std::mutex mutex;
    std::list<Engine *> engines;
    std::jthread worker{[this](std::stop_token stop) { run(stop); }};

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            if (stop.stop_requested())
                break;
            std::lock_guard registryLock(mutex);
            for (auto *engine : engines) {
                std::lock_guard engineLock(engine->mutex_);
                if (engine->state == State::Watering &&
                    std::chrono::steady_clock::now() >= engine->deadline) {
                    engine->valve.close();
                    engine->state = State::Idle;
                }
                try {
                    if (internal::LeakDetector::hasLeak(engine->valve))
                        engine->injectFaultUnlocked();
                } catch (const std::exception &) {
                    // A failed sensor must not terminate the process or report healthy.
                    engine->injectFaultUnlocked();
                }
            }
        }
    }
};

Engine::Registry &Engine::registry() {
    static Registry instance;
    return instance;
}

void Engine::setWsId(const std::string &id) {
    std::lock_guard lock(mutex_);
    wsId_ = id;
}

std::string Engine::wsId() const {
    std::lock_guard lock(mutex_);
    return wsId_;
}

Engine::Engine() : Engine(Config{}) {}

Engine::Engine(Config config) : config(config) {
    auto &entries = registry();
    std::lock_guard lock(entries.mutex);
    entries.engines.push_back(this);
}

Engine::~Engine() {
    auto &entries = registry();
    std::lock_guard lock(entries.mutex);
    entries.engines.remove(this);
}

std::expected<void, Engine::Error> Engine::start(internal::units::liters_min startFlux) {
    std::lock_guard lock(mutex_);
    if (!internal::validFlow(startFlux, valve.maxFlux)) {
        return std::unexpected(Error::invalidFlowRequest(startFlux));
    }

    if (blockedByServer) {
        return std::unexpected(Error{ErrorType::BlockedByCentral});
    }
    if (state == State::Fault) {
        return std::unexpected(Error{ErrorType::Fault});
    }
    if (state == State::Watering) {
        return std::unexpected(Error{ErrorType::AlreadyWatering});
    }

    // Idle engines always have a closed valve.
    assert(internal::FlowMeter::measure(valve) == 0);

    [[maybe_unused]] const auto result = valve.openBy(startFlux);
    assert(result);

    deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{config.irrigationTimeoutLimit};
    state = State::Watering;
    return {};
}

std::chrono::seconds Engine::timeToShutdown() const {
    std::lock_guard lock(mutex_);
    if (state != State::Watering)
        return std::chrono::seconds{0};
    const auto time = std::chrono::duration_cast<std::chrono::seconds>(
        deadline - std::chrono::steady_clock::now());
    return time < std::chrono::seconds{0} ? std::chrono::seconds{0} : time;
}

bool Engine::isRunning() const {
    std::lock_guard lock(mutex_);
    return state == State::Watering;
}

std::expected<void, Engine::Error> Engine::stop() {
    std::lock_guard lock(mutex_);
    if (state == State::Idle) {
        return std::unexpected(Error{ErrorType::AlreadyStopped});
    }
    valve.close();
    if (state == State::Watering)
        state = State::Idle;
    return {};
}

std::expected<void, Engine::Error> Engine::block(server_token /*token*/) {
    std::lock_guard lock(mutex_);
    blockedByServer = true;
    valve.close();
    if (state == State::Watering)
        state = State::Idle;
    return {};
}

std::expected<void, Engine::Error> Engine::unblock(server_token /*token*/) {
    std::lock_guard lock(mutex_);
    blockedByServer = false;
    return {};
}

void Engine::injectFault() {
    std::lock_guard lock(mutex_);
    injectFaultUnlocked();
}

void Engine::repair() {
    std::lock_guard lock(mutex_);
    if (state == State::Fault)
        state = State::Idle;
}

bool Engine::isHealthy() const {
    std::lock_guard lock(mutex_);
    return state != State::Fault;
}

internal::units::liters_min Engine::measureFlow() const {
    std::lock_guard lock(mutex_);
    return internal::FlowMeter::measure(valve);
}

void Engine::injectFaultUnlocked() {
    state = State::Fault;
    valve.close();
}
