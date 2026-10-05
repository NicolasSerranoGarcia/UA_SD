#pragma once

#include <chrono>
#include <cstdint>
#include <cassert>
#include <expected>
#include <optional>
#include <thread>
#include <mutex>
#include <functional>
#include <list>

#include "../valve/valve.hpp"
#include "../flow_meter/flow_meter.hpp"
#include "../leak_detector/leak_detector.hpp"
#include "../valid_flow.hpp"

using namespace std::literals;

class Engine {
    using seconds = std::uint64_t;
public:
    enum class ErrorType {
        /// A simulated station fault prevents irrigation. No additional payload.
        Fault,
        /// Requested flow is outside [0, maximum] or non-finite; inspect invalidFlow.
        InvalidFlow,
        /// Central has blocked irrigation. No additional payload.
        BlockedByCentral,
        /// Irrigation is already running. No additional payload.
        AlreadyWatering,
        /// Irrigation was already stopped. No additional payload
        AlreadyStopped
    };

    struct InvalidFlowDetails {
        internal::units::liters_min requested;
        internal::units::liters_min maximum;
    };

    /// Only InvalidFlow carries additional diagnostic details.
    struct Error {
        ErrorType type;
        std::optional<InvalidFlowDetails> invalidFlow = std::nullopt;

        static Error invalidFlowRequest(internal::units::liters_min requested) noexcept {
            return { ErrorType::InvalidFlow,
                     InvalidFlowDetails{requested, internal::Valve::maxFlux} };
        }
    };

    struct Config {
        seconds irrigationTimeoutLimit = 4000;
    };

    Engine() : Engine(Config{}) {}
    explicit Engine(Config config) : config(config) {
        std::lock_guard lock(EnginesMut);
        createdEngines.push_back(std::ref(*this));
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    ~Engine() {
        std::lock_guard lock(EnginesMut);
        createdEngines.remove_if([this](const auto& engine) {
            return &engine.get() == this;
        });
    }

    /// Returns InvalidFlow, BlockedByCentral, AlreadyWatering, or Fault.
    /// Input validation precedes permission/activity checks. Success means the
    /// requested valve operation succeeded and the engine entered Watering.
    [[nodiscard]] std::expected<void, Error> start(internal::units::liters_min startFlux = 40) {
        std::lock_guard lock(mutex_);
        if (!internal::validFlow(startFlux, valve.maxFlux)) {
            return std::unexpected(Error::invalidFlowRequest(startFlux));
        }

        if (blockedByServer) {
            return std::unexpected(Error{ ErrorType::BlockedByCentral });
        }
        if (state == State::Fault) {
            return std::unexpected(Error{ ErrorType::Fault });
        }
        if (state == State::Watering) {
            return std::unexpected(Error{ ErrorType::AlreadyWatering });
        }

        //should be 0 if there is no fault and the valve is closed
        assert(internal::FlowMeter::measure(valve) == 0);

        auto res = valve.openBy(startFlux);
        //on release assert is deleted so res will get a warning on unused variable
        (void) res;
        assert(res);

        deadline = std::chrono::steady_clock::now()
                 + std::chrono::seconds{config.irrigationTimeoutLimit};
        state = State::Watering;
        return {};
    }

    std::chrono::seconds timeToShutdown() const{
        const auto time = std::chrono::duration_cast<std::chrono::seconds>(deadline - std::chrono::steady_clock::now());
        return time < 0s ? 0s : time;
    }

    /// End irrigation without clearing a fault or changing Central's block.
    [[nodiscard]] std::expected<void, Error> stop() {
        std::lock_guard lock(mutex_);
        if (state == State::Idle) {
            return std::unexpected(Error{ ErrorType::AlreadyStopped });
        }
        valve.close();
        if (state == State::Watering) state = State::Idle;
        return {};
    }

    using server_token = std::uint32_t;
    [[nodiscard]] std::expected<void, Error> block(server_token /*token*/) {
        std::lock_guard lock(mutex_);
        blockedByServer = true;
        valve.close();
        if (state == State::Watering) state = State::Idle;
        return {};
    }

    [[nodiscard]] std::expected<void, Error> unblock(server_token /*token*/) {
        std::lock_guard lock(mutex_);
        blockedByServer = false;
        return {};
    }

    /// Resolving a fault leaves the station idle; a Central block still applies.
    void repair() {
        std::lock_guard lock(mutex_);
        if (state == State::Fault) state = State::Idle;
    }

    [[nodiscard]] bool isHealthy() const {
        std::lock_guard lock(mutex_);
        return state != State::Fault;
    }

    [[nodiscard]] internal::units::liters_min measureFlow() const {
        std::lock_guard lock(mutex_);
        return internal::FlowMeter::measure(valve);
    }

private:

    // Caller must hold mutex_.
    void injectFault(){
        state = State::Fault;
        valve.close();
    }

    enum class State { Idle, Watering, Fault };

    bool blockedByServer = false;

    internal::Valve valve;
    Config config;

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point deadline{};
    State state = State::Idle;

    // Lock order: registry mutex, then an engine's mutex.
    inline static std::mutex EnginesMut;
    inline static std::list<std::reference_wrapper<Engine>> createdEngines;

    class AsyncEnginePopulator {
    public:
        AsyncEnginePopulator() : worker(run) {}

    private:
        static void run(std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(100ms);
                if (stop.stop_requested()) break;

                std::lock_guard registryLock(EnginesMut);
                for (auto& reference : createdEngines) {
                    auto& engine = reference.get();
                    std::lock_guard engineLock(engine.mutex_);

                    const auto now = std::chrono::steady_clock::now();
                    if (engine.state == State::Watering && now >= engine.deadline) {
                        engine.valve.close();
                        engine.state = State::Idle;
                    }

                    if (internal::LeakDetector::hasLeak(engine.valve)) {
                        engine.injectFault();
                    }
                }
            }
        }

        std::jthread worker;
    };

    static const AsyncEnginePopulator asyncEngine;
};

inline const Engine::AsyncEnginePopulator Engine::asyncEngine{};
