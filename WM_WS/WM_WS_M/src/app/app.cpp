#include "app/app.hpp"

#include "hcp.hpp"
#include "nap/connection.hpp"
#include "nap/reporter.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>
#include <string>
#include <string_view>
#include <thread>

namespace app {
namespace {

void printAddresses(const ninttp::ResolvedAddresses &addresses) {
    for (const auto &endpoint : addresses.ipv4) {
        const auto address = endpoint.addressHostOrder();
        std::clog << "Central IPv4 endpoint: " << ((address >> 24) & 0xff) << '.'
                  << ((address >> 16) & 0xff) << '.' << ((address >> 8) & 0xff) << '.'
                  << (address & 0xff) << ':' << endpoint.portHostOrder() << '\n';
    }
    for (const auto &endpoint : addresses.ipv6) {
        const auto bytes = endpoint.addressBytes();
        std::clog << "Central IPv6 endpoint: [";
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

bool registerAndActivate(const ninttp::IPv4Endpoint &centralEndpoint, const std::string &wsId,
                         std::string_view location, std::chrono::milliseconds timeout) {
    nap::Connection<ninttp::IPv4Endpoint> conn(timeout);

    auto opened = conn.open(centralEndpoint);
    if (!opened) {
        std::cerr << "Error: connecting to central failed. Reason: " << opened.error().message()
                  << std::endl;
        return false;
    }

    auto registered = conn.sendRegistration(wsId, location);
    if (!registered) {
        std::cerr << "Error: sending registration failed. Reason: " << registered.error().message()
                  << std::endl;
        return false;
    }
    auto registrationResponse = conn.getResponse();
    if (!registrationResponse) {
        std::cerr << "Error: registration rejected or response failed. Reason: "
                  << registrationResponse.error().message() << std::endl;
        return false;
    }

    auto activated = conn.sendActivation(wsId);
    if (!activated) {
        std::cerr << "Error: sending activation failed. Reason: " << activated.error().message()
                  << std::endl;
        return false;
    }
    auto activationResponse = conn.getResponse();
    if (!activationResponse) {
        std::cerr << "Error: activation rejected or response failed. Reason: "
                  << activationResponse.error().message() << std::endl;
        return false;
    }
    return true;
}

hcp::Deadline deadline(std::chrono::milliseconds timeout) {
    return std::chrono::steady_clock::now() + timeout;
}

std::string session(const ninttp::IPv4Endpoint &engineEndpoint, const std::string &wsId,
                    std::chrono::milliseconds timeout, nap::Reporter &reporter) {
    // Return session failures so the caller can report KO and reconnect.
    ninttp::StreamSocket<ninttp::IPv4Endpoint> socket(ninttp::Protocol::Tcp, false);
    if (auto result = hcp::connectWithin(socket, engineEndpoint, deadline(timeout)); !result)
        return "connect: " + result.error().message();
    if (auto result = hcp::sendExact(socket, "ID " + wsId + "\n", deadline(timeout)); !result)
        return "identification send: " + result.error().message();
    std::array<char, 2> response{};
    if (auto result = hcp::receiveExact(socket, response, deadline(timeout)); !result)
        return "identification response: " + result.error().message();
    if (std::string_view(response.data(), response.size()) != hcp::okMsg)
        return "Engine rejected or returned an invalid WS-ID acknowledgement";

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds{1});
        if (auto result = hcp::sendExact(socket, "HC", deadline(timeout)); !result)
            return "health request: " + result.error().message();
        if (auto result = hcp::receiveExact(socket, response, deadline(timeout)); !result)
            return "health response: " + result.error().message();
        const std::string_view status(response.data(), response.size());
        if (status != hcp::okMsg && status != hcp::koMsg)
            return "invalid HCP health response";
        std::cout << status << std::endl;
        reporter.update(status == hcp::okMsg, status);
    }
}

} // namespace

int run(const Options &options) {
    const auto addresses = ninttp::dns::resolve(options.centralHostname, options.centralPort);
    if (!addresses) {
        std::cerr << "Error: resolving central hostname failed: "
                  << ninttp::SocketError{addresses.error()}.what() << '\n';
        return 1;
    }
    printAddresses(*addresses);
    if (addresses->ipv4.empty()) {
        std::cerr << "Error: central hostname resolved to no IPv4 endpoints\n";
        return 1;
    }
    const auto centralEndpoint = addresses->ipv4.front();
    if (!registerAndActivate(centralEndpoint, options.wsId, options.location, options.napTimeout))
        return 1;

    nap::Reporter reporter(centralEndpoint, options.wsId, options.napInterval, options.napTimeout);
    while (true) {
        std::string failure;
        try {
            failure = session(options.engineEndpoint, options.wsId, options.hcpTimeout, reporter);
        } catch (const ninttp::SocketError &error) {
            failure = error.what();
        }
        std::cerr << failure << '\n';
        reporter.update(false, failure);
        std::this_thread::sleep_for(options.hcpRetry);
    }
}

} // namespace app
