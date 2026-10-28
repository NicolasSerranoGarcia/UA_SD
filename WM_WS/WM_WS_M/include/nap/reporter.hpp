#pragma once

#include "nap/connection.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <memory>
#include <ninttp/endpoints.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace nap {

struct CentralStatus {
    bool available = false;
    std::uint64_t activationGeneration = 0;
    std::string lastError;
    bool registered = false;
    bool fatal = false;
    std::optional<ninttp::IPv4Endpoint> endpoint;
};

// Owns NAP reporting so a slow Central cannot suspend engine health checks.
class Reporter {
  public:
    Reporter(ninttp::IPv4Endpoint endpoint, std::string wsId, std::chrono::milliseconds interval,
             std::chrono::milliseconds timeout, std::string location = {}, std::string hostname = {});

    ~Reporter();

    Reporter(const Reporter &) = delete;
    Reporter &operator=(const Reporter &) = delete;
    Reporter(Reporter &&) = delete;
    Reporter &operator=(Reporter &&) = delete;

    void update(bool healthy, std::string_view reason);

    /// Zero until healthy recovery has been activated and every pending fault acknowledged.
    [[nodiscard]] std::uint64_t activationGeneration() const;
    [[nodiscard]] CentralStatus status() const;

  private:
    bool send(bool healthy, const std::string &reason);
    bool registerStation();
    bool resolve();
    bool failed(const Connection<ninttp::IPv4Endpoint>::Error &error);

    void run();

    ninttp::IPv4Endpoint endpoint_;
    std::string wsId_;
    std::string location_;
    std::string hostname_;
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds timeout_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    std::optional<bool> healthy_;
    std::string reason_;
    std::string faultReason_;
    std::uint64_t faultGeneration_ = 0;
    std::uint64_t acknowledgedFault_ = 0;
    std::uint64_t activationGeneration_ = 0;
    bool activated_ = false;
    CentralStatus status_;
    bool needsActivation_ = true;
    std::unique_ptr<Connection<ninttp::IPv4Endpoint>> connection_;
    std::chrono::steady_clock::time_point nextAttempt_{};
    std::thread worker_;
};

} // namespace nap
