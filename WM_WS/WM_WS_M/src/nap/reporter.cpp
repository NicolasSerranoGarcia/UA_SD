#include "logging.hpp"
#include "nap/reporter.hpp"

#include <ninttp/dns/dns.hpp>
#include <utility>

namespace nap {

Reporter::Reporter(ninttp::IPv4Endpoint endpoint, std::string wsId,
                   std::chrono::milliseconds interval, std::chrono::milliseconds timeout,
                   std::string location, std::string hostname)
    : endpoint_(endpoint), wsId_(std::move(wsId)), location_(std::move(location)),
      hostname_(std::move(hostname)), interval_(interval), timeout_(timeout),
      worker_(&Reporter::run, this) {}

Reporter::~Reporter() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    changed_.notify_one();
    worker_.join();
}

void Reporter::update(bool healthy, std::string_view reason) {
    {
        std::lock_guard lock(mutex_);
        if (!healthy_ || *healthy_ != healthy) {
            if (!healthy) { ++faultGeneration_; faultReason_ = reason; }
            nextAttempt_ = std::chrono::steady_clock::now();
        }
        healthy_ = healthy;
        if (!healthy) activated_ = false;
        reason_ = reason;
    }
    changed_.notify_one();
}

CentralStatus Reporter::status() const {
    std::lock_guard lock(mutex_);
    auto result = status_;
    result.activationGeneration = status_.available && activated_ && healthy_.value_or(false) &&
        faultGeneration_ == acknowledgedFault_ ? activationGeneration_ : 0;
    return result;
}

std::uint64_t Reporter::activationGeneration() const { return status().activationGeneration; }

bool Reporter::resolve() {
    if (!hostname_.empty()) {
        const auto addresses = ninttp::dns::resolve(hostname_, endpoint_.portHostOrder());
        if (!addresses || addresses->ipv4.empty()) {
            std::lock_guard lock(mutex_);
            status_.available = false;
            status_.lastError = "Cannot resolve Central's address. Retrying automatically.";
            return false;
        }
        endpoint_ = addresses->ipv4.front();
    }
    std::lock_guard lock(mutex_);
    status_.endpoint = endpoint_;
    return true;
}

bool Reporter::failed(const Connection<ninttp::IPv4Endpoint>::Error &error) {
    using Type = Connection<ninttp::IPv4Endpoint>::Error::Type;
    std::lock_guard lock(mutex_);
    status_.available = false;
    status_.lastError = error.message();
    status_.fatal = error.type != Type::Io && error.type != Type::Closed && error.type != Type::Timeout;
    if (error.type == Type::Nack && error.nackCode == 2 && !location_.empty()) {
        status_.registered = false;
        status_.fatal = false;
    }
    logging::record() << "[Central][NAP] ws=" << wsId_ << " reporting failed: " << status_.lastError << std::endl;
    return false;
}

bool Reporter::registerStation() {
    using CentralConnection = Connection<ninttp::IPv4Endpoint>;
    if (!resolve()) return false;
    CentralConnection registration(timeout_);
    auto result = registration.open(endpoint_);
    if (result) result = registration.sendRegistration(wsId_, location_);
    if (!result) return failed(result.error());
    const auto response = registration.getResponse();
    if (!response && !(response.error().type == CentralConnection::Error::Type::Nack &&
                       response.error().nackCode == 1))
        return failed(response.error());
    logging::record() << "[Central][NAP] ws=" << wsId_ << " registration acknowledged" << std::endl;
    return true;
}

bool Reporter::send(bool healthy, const std::string &reason) {
    using CentralConnection = Connection<ninttp::IPv4Endpoint>;
    if (healthy && needsActivation_) {
        if (!resolve()) return false;
        CentralConnection activation(timeout_);
        auto result = activation.open(endpoint_);
        if (result) result = activation.sendActivation(wsId_);
        if (!result) return failed(result.error());
        const auto response = activation.getResponse();
        if (!response) return failed(response.error());
        needsActivation_ = false;
        logging::record() << "[Central][NAP] ws=" << wsId_ << " activation acknowledged" << std::endl;
    }
    std::expected<void, CentralConnection::Error> result;
    if (!connection_) {
        if (!resolve()) return false;
        connection_ = std::make_unique<CentralConnection>(timeout_);
        result = connection_->open(endpoint_);
    }
    if (result) result = connection_->sendStatus(wsId_, healthy);
    if (!result) return failed(result.error());
    const auto response = connection_->getResponse();
    if (!response) return failed(response.error());
    logging::record() << "[Central][NAP] ws=" << wsId_ << " status acknowledged: state="
                      << (healthy ? "healthy" : "fault") << " reason=" << reason << std::endl;
    if (!healthy) { connection_.reset(); needsActivation_ = true; }
    return true;
}

void Reporter::run() {
    std::unique_lock lock(mutex_);
    status_.registered = location_.empty();
    while (!stopping_) {
        if (status_.fatal) { changed_.wait(lock); continue; }
        if (status_.registered && faultGeneration_ == acknowledgedFault_ && (!healthy_ || !*healthy_)) {
            changed_.wait(lock);
            continue;
        }
        const auto due = nextAttempt_;
        if (std::chrono::steady_clock::now() < due) { changed_.wait_until(lock, due); continue; }
        const bool registration = !status_.registered;
        const auto generation = faultGeneration_;
        const bool healthy = generation == acknowledgedFault_;
        const bool activation = healthy && needsActivation_;
        const auto reason = healthy ? reason_ : faultReason_;
        lock.unlock();
        bool acknowledged = false;
        try { acknowledged = registration ? registerStation() : send(healthy, reason); }
        catch (const ninttp::SocketError &error) {
            std::lock_guard guard(mutex_); status_.lastError = error.what();
        } catch (const std::exception &error) {
            std::lock_guard guard(mutex_); status_.lastError = error.what(); status_.fatal = true;
        }
        if (!acknowledged) { connection_.reset(); needsActivation_ = true; }
        lock.lock();
        status_.available = acknowledged;
        if (acknowledged) status_.lastError.clear();
        if (registration && acknowledged) status_.registered = true;
        if (!acknowledged || !healthy) activated_ = false;
        if (!registration && acknowledged && !healthy) acknowledgedFault_ = generation;
        if (!registration && acknowledged && healthy && healthy_.value_or(false) && generation == faultGeneration_) {
            if (activation) ++activationGeneration_;
            activated_ = true;
        }
        // A successful registration can proceed directly to the pending health report.
        if (registration && acknowledged) nextAttempt_ = {};
        else if (nextAttempt_ == due) nextAttempt_ = std::chrono::steady_clock::now() + interval_;
    }
}

} // namespace nap
