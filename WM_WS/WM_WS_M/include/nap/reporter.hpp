#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <ninttp/endpoints.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace nap {

// Owns NAP reporting so a slow Central cannot suspend engine health checks.
class Reporter {
  public:
    Reporter(ninttp::IPv4Endpoint endpoint, std::string wsId, std::chrono::milliseconds interval,
             std::chrono::milliseconds timeout);

    ~Reporter();

    Reporter(const Reporter &) = delete;
    Reporter &operator=(const Reporter &) = delete;
    Reporter(Reporter &&) = delete;
    Reporter &operator=(Reporter &&) = delete;

    void update(bool healthy, std::string_view reason);

  private:
    bool send(bool healthy, const std::string &reason);

    void run();

    ninttp::IPv4Endpoint endpoint_;
    std::string wsId_;
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds timeout_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    std::optional<bool> healthy_;
    std::string reason_;
    std::string faultReason_;
    std::uint64_t faultGeneration_ = 0;
    std::uint64_t acknowledgedFault_ = 0;
    std::chrono::steady_clock::time_point nextAttempt_{};
    std::thread worker_;
};

} // namespace nap
