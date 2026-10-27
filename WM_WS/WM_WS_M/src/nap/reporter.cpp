#include "logging.hpp"
#include "nap/reporter.hpp"

#include "nap/connection.hpp"
#include <exception>
#include <iostream>
#include <syncstream>
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
        if (!healthy)
            activated_ = false;
        reason_ = reason;
    }
    changed_.notify_one();
}

std::uint64_t Reporter::activationGeneration() const {
    std::lock_guard lock(mutex_);
    return activated_ && healthy_.value_or(false) && faultGeneration_ == acknowledgedFault_
               ? activationGeneration_ : 0;
}

bool Reporter::send(bool healthy, const std::string &reason) {
    using CentralConnection = Connection<ninttp::IPv4Endpoint>;
    if (healthy && needsActivation_) {
        CentralConnection activation(timeout_);
        auto result = activation.open(endpoint_);
        if (result)
            result = activation.sendActivation(wsId_);
        if (result) {
            const auto response = activation.getResponse();
            if (!response)
                result = std::unexpected(response.error());
        }
        if (!result) {
            logging::record() << "[Central][NAP] ws=" << wsId_
                                       << " activation failed: " << result.error().message() << '\n';
            return false;
        }
        needsActivation_ = false;
        {
            std::lock_guard lock(mutex_);
            activated_ = true;
            ++activationGeneration_;
        }
        logging::record() << "[Central][NAP] ws=" << wsId_
                                   << " activation acknowledged" << std::endl;
    }
    std::expected<void, CentralConnection::Error> result;
    if (!connection_) {
        connection_ = std::make_unique<CentralConnection>(timeout_);
        result = connection_->open(endpoint_);
    }
    if (result)
        result = connection_->sendStatus(wsId_, healthy);
    if (!result) {
        logging::record() << "[Central][NAP] ws=" << wsId_
                                   << " status send failed: " << result.error().message() << '\n';
        return false;
    }
    auto response = connection_->getResponse();
    if (!response) {
        logging::record() << "[Central][NAP] ws=" << wsId_
                                   << " status acknowledgement failed: "
                                   << response.error().message() << '\n';
        return false;
    }
    logging::record() << "[Central][NAP] ws=" << wsId_
                               << " status acknowledged: state=" << (healthy ? "healthy" : "fault")
                               << " reason=" << reason << std::endl;
    if (!healthy) {
        connection_.reset();
        needsActivation_ = true;
    }
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
            logging::record() << "[Central][NAP] ws=" << wsId_
                                       << " reporting socket failed: " << error.what() << '\n';
        } catch (const std::exception &error) {
            logging::record() << "[Central][NAP] ws=" << wsId_
                                       << " reporting failed: " << error.what() << '\n';
        }
        if (!acknowledged) {
            connection_.reset();
            needsActivation_ = true;
        }
        lock.lock();
        if (!acknowledged || !healthy)
            activated_ = false;
        if (acknowledged && !healthy)
            acknowledgedFault_ = generation;
        // Preserve an immediate report requested while this exchange was running.
        if (nextAttempt_ == due)
            nextAttempt_ = std::chrono::steady_clock::now() + interval_;
    }
}

} // namespace nap
