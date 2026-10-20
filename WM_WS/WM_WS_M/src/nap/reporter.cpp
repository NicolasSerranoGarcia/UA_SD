#include "nap/reporter.hpp"

#include "nap/connection.hpp"
#include <exception>
#include <iostream>
#include <utility>

namespace nap {

Reporter::Reporter(ninttp::IPv4Endpoint endpoint, std::string wsId,
                   std::chrono::milliseconds interval, std::chrono::milliseconds timeout)
    : endpoint_(endpoint), wsId_(std::move(wsId)), interval_(interval), timeout_(timeout),
      worker_(&Reporter::run, this) {}

Reporter::~Reporter() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_one();
    worker_.join();
}

void Reporter::update(bool healthy, std::string_view reason) {
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

bool Reporter::send(bool healthy, const std::string &reason) {
    Connection<ninttp::IPv4Endpoint> conn(timeout_);
    auto result = conn.open(endpoint_);
    if (result)
        result = conn.sendStatus(wsId_, healthy);
    if (!result) {
        std::cerr << "Error: sending central status failed. Reason: " << result.error().message()
                  << '\n';
        return false;
    }
    auto response = conn.getResponse();
    if (!response) {
        std::cerr << "Error: central status response failed. Reason: " << response.error().message()
                  << '\n';
        return false;
    }
    std::cout << "CENTRAL hook: ws=" << wsId_ << " state=" << (healthy ? "healthy" : "fault")
              << " reason=" << reason << std::endl;
    return true;
}

void Reporter::run() {
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
        bool acknowledged = false;
        try {
            acknowledged = send(healthy, reason);
        } catch (const ninttp::SocketError &error) {
            std::cerr << "Error: central reporting socket: " << error.what() << '\n';
        } catch (const std::exception &error) {
            std::cerr << "Error: central reporting: " << error.what() << '\n';
        }
        lock.lock();
        if (acknowledged && !healthy)
            acknowledgedFault_ = generation;
        // Preserve an immediate report requested while this exchange was running.
        if (nextAttempt_ == due)
            nextAttempt_ = std::chrono::steady_clock::now() + interval_;
    }
}

} // namespace nap
