#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <string>

#include "engine/units.hpp"
#include "engine/valve/valve.hpp"

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
        /// Monitor is disconnected. No additional payload.
        MonitorDisconnected,
        /// External activation is required. No additional payload.
        NotActivated,
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
            return {ErrorType::InvalidFlow,
                    InvalidFlowDetails{requested, internal::Valve::maxFlux}};
        }
    };

    void setWsId(const std::string &id);

    [[nodiscard]] std::string wsId() const;

    /// Losing Monitor stops irrigation and revokes permission to start or publish.
    void setMonitorConnected(bool connected);

    [[nodiscard]] bool isActivated() const;

    [[nodiscard]] bool canPublish() const;

    struct Reading {
        double flowLitersPerMinute;
        double volumeLiters;
        double elapsedSeconds;
    };

    /// Final totals remain readable after stopping, disconnecting, or detecting a leak.
    [[nodiscard]] Reading reading() const;

    /// NAP/HCP activation cannot remove a deliberate Central block.
    [[nodiscard]] std::expected<void, Error> activateFromMonitor();

    struct Config {
        seconds irrigationTimeoutLimit = 4000;
    };

    Engine();
    explicit Engine(Config config);

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    ~Engine();

    /// Returns InvalidFlow, BlockedByCentral, AlreadyWatering, or Fault.
    /// Input validation precedes permission/activity checks. Success means the
    /// requested valve operation succeeded and the engine entered Watering.
    [[nodiscard]] std::expected<void, Error> start(internal::units::liters_min startFlux = 40);

    /// A null duration is indefinite irrigation, as specified by Central-ops.
    [[nodiscard]] std::expected<void, Error> start(
        internal::units::liters_min startFlux, std::optional<std::chrono::seconds> duration);

    [[nodiscard]] std::chrono::seconds timeToShutdown() const;

    [[nodiscard]] bool isRunning() const;

    /// End irrigation without clearing a fault or changing Central's block.
    [[nodiscard]] std::expected<void, Error> stop();

    using server_token = std::uint32_t;
    [[nodiscard]] std::expected<void, Error> block(server_token /*token*/);

    [[nodiscard]] std::expected<void, Error> unblock(server_token /*token*/);

    /// Simulate a fault and immediately stop irrigation.
    void injectFault();

    /// Resolving a fault leaves the station idle; a Central block still applies.
    void repair();

    [[nodiscard]] bool isHealthy() const;

    /// Report a pending leak at least once, even if repaired between HCP polls.
    [[nodiscard]] bool checkHealth();

    [[nodiscard]] internal::units::liters_min measureFlow() const;

  private:
    // Caller must hold mutex_.
    void injectFaultUnlocked();
    void stopWateringUnlocked();
    [[nodiscard]] Reading readingUnlocked() const;

    enum class State { Idle, Watering, Fault };

    bool blockedByServer = false;
    bool monitorConnected_ = false;
    bool activated_ = false;
    bool monitorAuthorized_ = false;
    bool faultPending_ = false;
    bool recoveryRequired_ = false;

    internal::Valve valve;
    Config config;
    std::string wsId_;

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point deadline{};
    std::chrono::steady_clock::time_point started_{};
    Reading finalReading_{};
    State state = State::Idle;

    struct Registry;
    static Registry &registry();
};
