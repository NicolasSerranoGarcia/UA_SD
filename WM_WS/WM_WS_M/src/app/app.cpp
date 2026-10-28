#include "logging.hpp"
#include "app/app.hpp"

#include "hcp.hpp"
#include "nap/connection.hpp"
#include "nap/reporter.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>
#include <string>
#include <string_view>
#include <syncstream>
#include <thread>

namespace app {
namespace {

hcp::Deadline deadline(std::chrono::milliseconds timeout) {
    return std::chrono::steady_clock::now() + timeout;
}

std::string session(const ninttp::IPv4Endpoint &engineEndpoint, const std::string &wsId,
                    std::chrono::milliseconds timeout, nap::Reporter &reporter,
                    std::stop_token stop, Status &snapshot, const StatusObserver &observer) {
    // Return session failures so the caller can report KO and reconnect.
    ninttp::StreamSocket<ninttp::IPv4Endpoint> socket(ninttp::Protocol::Tcp, false);
    if (auto result = hcp::connectWithin(socket, engineEndpoint, deadline(timeout)); !result)
        return "connect: " + result.error().message();
    if (stop.stop_requested())
        return {};
    if (auto result = hcp::sendExact(socket, "ID " + wsId + "\n", deadline(timeout)); !result)
        return "identification send: " + result.error().message();
    if (stop.stop_requested())
        return {};
    std::array<char, 2> response{};
    if (auto result = hcp::receiveExact(socket, response, deadline(timeout)); !result)
        return "identification response: " + result.error().message();
    if (std::string_view(response.data(), response.size()) != hcp::okMsg)
        return "Engine rejected or returned an invalid WS-ID acknowledgement";

    std::mutex mutex;
    std::condition_variable_any wake;
    std::uint64_t activationGeneration = 0;
    bool centralDownSent = false;
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, stop, std::chrono::seconds{1}, [] { return false; });
        if (stop.stop_requested())
            break;
        if (auto result = hcp::sendExact(socket, "HC", deadline(timeout)); !result)
            return "health request: " + result.error().message();
        if (auto result = hcp::receiveExact(socket, response, deadline(timeout)); !result)
            return "health response: " + result.error().message();
        const std::string_view status(response.data(), response.size());
        if (status != hcp::okMsg && status != hcp::koMsg)
            return "invalid HCP health response";
        logging::record() << "[Engine][HCP] ws=" << wsId << " health=" << status
                                   << std::endl;
        reporter.update(status == hcp::okMsg, status);
        const auto central = reporter.status();
        if (central.fatal) return "Central rejected registration or reporting: " + central.lastError;
        snapshot.centralAvailable = central.available;
        snapshot.centralError = central.lastError;
        snapshot.centralEndpoint = central.endpoint;
        const auto acknowledgedActivation = central.activationGeneration;
        if (!central.available && activationGeneration != 0 && !centralDownSent) {
            if (auto result = hcp::sendExact(socket, "DC", deadline(timeout)); !result)
                return "Central availability send: " + result.error().message();
            std::array<char, 2> acknowledgement{};
            if (auto result = hcp::receiveExact(socket, acknowledgement, deadline(timeout)); !result)
                return "Central availability response: " + result.error().message();
            if (std::string_view{acknowledgement.data(), acknowledgement.size()} != hcp::okMsg)
                return "Engine rejected Central availability notification";
            centralDownSent = true;
        }
        if (central.available && status == hcp::okMsg && acknowledgedActivation != 0 &&
            acknowledgedActivation != activationGeneration) {
            if (auto result = hcp::sendExact(socket, "AC", deadline(timeout)); !result)
                return "activation send: " + result.error().message();
            std::array<char, 2> activationResponse{};
            if (auto result = hcp::receiveExact(socket, activationResponse, deadline(timeout)); !result)
                return "activation response: " + result.error().message();
            // A deliberate Central block is healthy but cannot be cleared through HCP.
            if (std::string_view{activationResponse.data(), activationResponse.size()} != hcp::okMsg &&
                std::string_view{activationResponse.data(), activationResponse.size()} != hcp::koMsg)
                return "invalid HCP activation response";
            // KO may retain an administrative block while still acknowledging Central availability.
            activationGeneration = acknowledgedActivation;
            centralDownSent = false;
        }
        snapshot.phase = Phase::Monitoring;
        snapshot.lastError.clear();
        snapshot.health = status == hcp::okMsg ? Health::Ok : Health::Ko;
        snapshot.lastHealth = std::chrono::system_clock::now();
        if (observer)
            observer(snapshot);
    }
    return {};
}

} // namespace

int run(const Options &options, std::stop_token stop, StatusObserver observer) {
    Status snapshot;
    const auto publish = [&] {
        if (observer)
            observer(snapshot);
    };
    const auto stopped = [&] {
        snapshot.phase = Phase::Stopped;
        publish();
        return 0;
    };
    const auto failed = [&](std::string reason) {
        if (stop.stop_requested())
            return stopped();
        logging::fatal("Monitor", "Monitoring could not start or continue.", reason,
                       "Check Central's address/port, station registration, and connectivity, then restart Monitor.");
        snapshot.phase = Phase::Failed;
        snapshot.lastError = std::move(reason);
        publish();
        return 1;
    };
    if (stop.stop_requested())
        return stopped();
    publish();
    nap::Reporter reporter(ninttp::IPv4Endpoint::loopback(options.centralPort), options.wsId,
                           options.napInterval, options.napTimeout, options.location, options.centralHostname);
    snapshot.phase = Phase::Monitoring;
    publish();
    std::mutex mutex;
    std::condition_variable_any wake;
    while (!stop.stop_requested()) {
        std::string failure;
        try {
            auto endpoint = options.engineEndpoint;
            if (!options.engineHostname.empty()) {
                const auto addresses = ninttp::dns::resolve(options.engineHostname, endpoint.portHostOrder());
                if (!addresses || addresses->ipv4.empty())
                    failure = "Cannot resolve Engine's address. Retrying automatically.";
                else
                    endpoint = addresses->ipv4.front();
            }
            if (failure.empty())
                failure = session(endpoint, options.wsId, options.hcpTimeout, reporter, stop, snapshot, observer);
        } catch (const ninttp::SocketError &error) {
            failure = error.what();
        }
        if (stop.stop_requested())
            break;
        const auto central = reporter.status();
        snapshot.centralAvailable = central.available;
        snapshot.centralError = central.lastError;
        snapshot.centralEndpoint = central.endpoint;
        if (central.fatal) return failed(central.lastError);
        logging::record() << "[Engine][HCP] ws=" << options.wsId
                                   << " session failed: " << failure << '\n';
        reporter.update(false, failure);
        snapshot.phase = Phase::Retrying;
        snapshot.health = Health::Unknown;
        snapshot.lastError = failure;
        publish();
        std::unique_lock lock(mutex);
        wake.wait_for(lock, stop, options.hcpRetry, [] { return false; });
    }
    return stopped();
}

} // namespace app
