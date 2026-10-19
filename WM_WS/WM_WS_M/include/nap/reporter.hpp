#pragma once

#include "connection.hpp"
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

namespace nap {

// Owns NAP reporting so a slow Central cannot suspend engine health checks.
class Reporter {
public:
    Reporter(ninttp::IPv4Endpoint endpoint, std::string wsId,
             std::chrono::milliseconds interval, std::chrono::milliseconds timeout)
        : endpoint_(endpoint), wsId_(std::move(wsId)), interval_(interval), timeout_(timeout),
          worker_(&Reporter::run, this) {}

    ~Reporter() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_one();
        worker_.join();
    }

    void update(bool healthy, std::string_view reason) {
        {
            std::lock_guard lock(mutex_);
            if (!healthy_ || *healthy_ != healthy) {
                if (!healthy) {
                    ++faultGeneration_;
                    faultReason_ = reason;
                }
                nextAttempt_ = std::chrono::steady_clock::now();
            }
            healthy_ = healthy;
            reason_ = reason;
        }
        changed_.notify_one();
    }

private:
    bool send(bool healthy, const std::string& reason) {
        Connection<ninttp::IPv4Endpoint> conn(timeout_);
        auto result = conn.open(endpoint_);
        if (result) result = conn.sendStatus(healthy);
        if (!result) {
            std::cerr << "Error: sending central status failed. Reason: "
                      << result.error().message() << '\n';
            return false;
        }
        auto response = conn.getResponse();
        if (!response) {
            std::cerr << "Error: central status response failed. Reason: "
                      << response.error().message() << '\n';
            return false;
        }
        std::cout << "CENTRAL hook: ws=" << wsId_ << " state="
                  << (healthy ? "healthy" : "fault") << " reason=" << reason << std::endl;
        return true;
    }

    void run() {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            // A fault remains pending through recovery until Central acknowledges it.
            if (faultGeneration_ == acknowledgedFault_ && (!healthy_ || !*healthy_)) {
                changed_.wait(lock);
                continue;
            }
            const auto due = nextAttempt_;
            if (std::chrono::steady_clock::now() < due) {
                changed_.wait_until(lock, due);
                continue;
            }
            const auto generation = faultGeneration_;
            const bool healthy = generation == acknowledgedFault_;
            const auto reason = healthy ? reason_ : faultReason_;
            lock.unlock();
            const bool acknowledged = send(healthy, reason);
            lock.lock();
            if (acknowledged && !healthy) acknowledgedFault_ = generation;
            // Preserve an immediate report requested while this exchange was running.
            if (nextAttempt_ == due)
                nextAttempt_ = std::chrono::steady_clock::now() + interval_;
        }
    }

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
