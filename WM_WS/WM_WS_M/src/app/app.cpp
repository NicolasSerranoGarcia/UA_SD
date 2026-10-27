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

void printAddresses(const ninttp::ResolvedAddresses &addresses) {
    for (const auto &endpoint : addresses.ipv4) {
        const auto address = endpoint.addressHostOrder();
        std::clog << "[Central][DNS] IPv4 endpoint: " << ((address >> 24) & 0xff) << '.'
                  << ((address >> 16) & 0xff) << '.' << ((address >> 8) & 0xff) << '.'
                  << (address & 0xff) << ':' << endpoint.portHostOrder() << '\n';
    }
    for (const auto &endpoint : addresses.ipv6) {
        const auto bytes = endpoint.addressBytes();
        std::clog << "[Central][DNS] IPv6 endpoint: [";
        const auto flags = std::clog.flags();
        std::clog << std::hex;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            if (i != 0)
                std::clog << ':';
            std::clog << ((static_cast<unsigned int>(bytes[i]) << 8) |
                          static_cast<unsigned int>(bytes[i + 1]));
        }
        std::clog.flags(flags);
        std::clog << "]:" << endpoint.portHostOrder() << '\n';
    }
}

std::expected<void, std::string>
registerStation(const ninttp::IPv4Endpoint &centralEndpoint, const std::string &wsId,
                    std::string_view location, std::chrono::milliseconds timeout,
                    std::stop_token stop) {
    using Connection = nap::Connection<ninttp::IPv4Endpoint>;
    {
        Connection registration(timeout);

        auto opened = registration.open(centralEndpoint);
        if (!opened) {
            return std::unexpected("connecting to central failed. Reason: " + opened.error().message());
        }
        if (stop.stop_requested())
            return {};

        auto registered = registration.sendRegistration(wsId, location);
        if (!registered) {
            return std::unexpected("sending registration failed. Reason: " + registered.error().message());
        }
        if (stop.stop_requested())
            return {};
        auto response = registration.getResponse();
        // An existing station still needs activation when Monitor restarts.
        const bool alreadyRegistered = !response && response.error().type == Connection::Error::Type::Nack &&
                                       response.error().nackCode == 1;
        if (!response && !alreadyRegistered) {
            return std::unexpected("registration rejected or response failed. Reason: " + response.error().message());
        }
        if (stop.stop_requested())
            return {};
        std::osyncstream(std::cout) << "[Central][NAP] ws=" << wsId
                                   << (alreadyRegistered ? " already registered; proceeding to activation"
                                                         : " registration acknowledged")
                                   << std::endl;
    }

    // Activation follows a healthy Engine response, owned by the NAP reporter.
    return {};
}

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
        std::osyncstream(std::cout) << "[Engine][HCP] ws=" << wsId << " health=" << status
                                   << std::endl;
        reporter.update(status == hcp::okMsg, status);
        const auto acknowledgedActivation = reporter.activationGeneration();
        if (status == hcp::okMsg && acknowledgedActivation != 0 &&
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
            if (std::string_view{activationResponse.data(), activationResponse.size()} == hcp::okMsg)
                activationGeneration = acknowledgedActivation;
        }
        snapshot.phase = Phase::Monitoring;
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
        std::cerr << "Error: " << reason << '\n';
        snapshot.phase = Phase::Failed;
        snapshot.lastError = std::move(reason);
        publish();
        return 1;
    };
    if (stop.stop_requested())
        return stopped();
    publish();
    const auto addresses = ninttp::dns::resolve(options.centralHostname, options.centralPort);
    if (stop.stop_requested())
        return stopped();
    if (!addresses) {
        return failed("resolving central hostname failed: " +
                      std::string(ninttp::SocketError{addresses.error()}.what()));
    }
    printAddresses(*addresses);
    if (addresses->ipv4.empty()) {
        return failed("central hostname resolved to no IPv4 endpoints");
    }
    const auto centralEndpoint = addresses->ipv4.front();
    snapshot.centralEndpoint = centralEndpoint;
    snapshot.phase = Phase::Registering;
    publish();
    if (stop.stop_requested())
        return stopped();
    if (auto result = registerStation(centralEndpoint, options.wsId, options.location,
                                          options.napTimeout, stop); !result)
        return failed(result.error());
    if (stop.stop_requested())
        return stopped();

    snapshot.phase = Phase::Monitoring;
    publish();
    nap::Reporter reporter(centralEndpoint, options.wsId, options.napInterval, options.napTimeout);
    std::mutex mutex;
    std::condition_variable_any wake;
    while (!stop.stop_requested()) {
        std::string failure;
        try {
            failure = session(options.engineEndpoint, options.wsId, options.hcpTimeout, reporter,
                              stop, snapshot, observer);
        } catch (const ninttp::SocketError &error) {
            failure = error.what();
        }
        if (stop.stop_requested())
            break;
        std::osyncstream(std::cerr) << "[Engine][HCP] ws=" << options.wsId
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
