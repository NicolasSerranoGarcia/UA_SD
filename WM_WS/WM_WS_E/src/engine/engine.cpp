#include "engine/engine.hpp"

#include "engine/flow_meter/flow_meter.hpp"
#include "engine/leak_detector/leak_detector.hpp"
#include "engine/valid_flow.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>
#include <algorithm>
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
                    engine->stopWateringUnlocked();
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

void Engine::setMonitorConnected(bool connected) {
    std::lock_guard lock(mutex_);
    monitorConnected_ = connected;
    if (!connected) {
        centralAvailable_ = false;
        activated_ = false;
        monitorAuthorized_ = false;
        stopWateringUnlocked();
    }
}

bool Engine::isActivated() const {
    std::lock_guard lock(mutex_);
    return activated_;
}

bool Engine::isMonitorConnected() const {
    std::lock_guard lock(mutex_);
    return monitorConnected_;
}

bool Engine::canPublish() const {
    std::lock_guard lock(mutex_);
    return monitorConnected_ && monitorAuthorized_;
}

void Engine::setCentralAvailable(bool available) {
    std::lock_guard lock(mutex_);
    centralAvailable_ = available;
}

bool Engine::isCentralAvailable() const {
    std::lock_guard lock(mutex_);
    return centralAvailable_;
}

bool Engine::isBlocked() const {
    std::lock_guard lock(mutex_);
    return blockedByServer;
}

std::expected<void, Engine::Error> Engine::activateFromMonitor() {
    std::lock_guard lock(mutex_);
    if (!monitorConnected_)
        return std::unexpected(Error{ErrorType::MonitorDisconnected});
    if (state == State::Fault || faultPending_)
        return std::unexpected(Error{ErrorType::Fault});
    monitorAuthorized_ = true;
    centralAvailable_ = true;
    recoveryRequired_ = false;
    if (blockedByServer)
        return std::unexpected(Error{ErrorType::BlockedByCentral});
    activated_ = true;
    return {};
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

std::expected<void, Engine::Error> Engine::start(internal::units::liters_min startFlux,
                                               std::optional<std::chrono::seconds> duration) {
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
    if (!monitorConnected_) {
        return std::unexpected(Error{ErrorType::MonitorDisconnected});
    }
    if (!activated_) {
        return std::unexpected(Error{ErrorType::NotActivated});
    }
    if (!centralAvailable_)
        return std::unexpected(Error{ErrorType::CentralUnavailable});
    if (state == State::Watering) {
        return std::unexpected(Error{ErrorType::AlreadyWatering});
    }

    // Idle engines always have a closed valve.
    assert(internal::FlowMeter::measure(valve) == 0);

    [[maybe_unused]] const auto result = valve.openBy(startFlux);
    assert(result);

    started_ = std::chrono::steady_clock::now();
    deadline = duration ? started_ + *duration : std::chrono::steady_clock::time_point::max();
    finalReading_ = {};
    committedReading_ = {};
    ++meterGeneration_;
    state = State::Watering;
    return {};
}

std::expected<void, Engine::Error> Engine::start(internal::units::liters_min startFlux) {
    return start(startFlux, std::chrono::seconds{config.irrigationTimeoutLimit});
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
    stopWateringUnlocked();
    return {};
}

std::expected<void, Engine::Error> Engine::block(server_token /*token*/) {
    std::lock_guard lock(mutex_);
    if (state == State::Fault)
        return std::unexpected(Error{ErrorType::Fault});
    blockedByServer = true;
    activated_ = false;
    stopWateringUnlocked();
    return {};
}

void Engine::restoreBlock() {
    std::lock_guard lock(mutex_);
    blockedByServer = true;
    activated_ = false;
    stopWateringUnlocked();
}

std::expected<void, Engine::Error> Engine::unblock(server_token /*token*/) {
    std::lock_guard lock(mutex_);
    if (!monitorConnected_)
        return std::unexpected(Error{ErrorType::MonitorDisconnected});
    if (state == State::Fault)
        return std::unexpected(Error{ErrorType::Fault});
    if (!monitorAuthorized_ || recoveryRequired_)
        return std::unexpected(Error{ErrorType::NotActivated});
    if (!centralAvailable_)
        return std::unexpected(Error{ErrorType::CentralUnavailable});
    blockedByServer = false;
    activated_ = true;
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

bool Engine::checkHealth() {
    std::lock_guard lock(mutex_);
    const bool healthy = state != State::Fault && !faultPending_;
    faultPending_ = false;
    return healthy;
}

internal::units::liters_min Engine::measureFlow() const {
    std::lock_guard lock(mutex_);
    return static_cast<internal::units::liters_min>(readingUnlocked().flowLitersPerMinute);
}

void Engine::injectFaultUnlocked() {
    stopWateringUnlocked();
    state = State::Fault;
    faultPending_ = true;
    activated_ = false;
    recoveryRequired_ = true;
    valve.close();
}

Engine::Reading Engine::rawReadingUnlocked() const {
    if (state != State::Watering)
        return finalReading_;
    const double elapsed = std::max(0.0, std::chrono::duration<double>(
        std::min(std::chrono::steady_clock::now(), deadline) - started_).count());
    return {internal::FlowMeter::measure(valve, elapsed),
            internal::FlowMeter::volume(valve, elapsed), elapsed};
}

Engine::Reading Engine::readingUnlocked() const {
    return durableMetering_ ? committedReading_ : rawReadingUnlocked();
}

void Engine::enableDurableMetering() {
    std::lock_guard lock(mutex_);
    durableMetering_ = true;
}

Engine::MeterSample Engine::sampleMeter() const {
    std::lock_guard lock(mutex_);
    return {rawReadingUnlocked(), meterGeneration_};
}

void Engine::commitMeter(const MeterSample &sample) {
    std::lock_guard lock(mutex_);
    if (sample.generation != meterGeneration_ || sample.reading.elapsedSeconds < committedReading_.elapsedSeconds)
        return;
    committedReading_ = sample.reading;
    if (state != State::Watering)
        committedReading_.flowLitersPerMinute = 0;
}

void Engine::restoreMeter(Reading reading) {
    std::lock_guard lock(mutex_);
    if (state == State::Watering || !std::isfinite(reading.volumeLiters) || reading.volumeLiters < 0 ||
        !std::isfinite(reading.elapsedSeconds) || reading.elapsedSeconds < 0)
        throw std::invalid_argument("Invalid recovered irrigation measurements");
    reading.flowLitersPerMinute = 0;
    finalReading_ = committedReading_ = reading;
}

Engine::Reading Engine::reading() const {
    std::lock_guard lock(mutex_);
    return readingUnlocked();
}

void Engine::stopWateringUnlocked() {
    if (state == State::Watering) {
        finalReading_ = rawReadingUnlocked();
        finalReading_.flowLitersPerMinute = 0;
        committedReading_.flowLitersPerMinute = 0;
        state = State::Idle;
    }
    valve.close();
}
