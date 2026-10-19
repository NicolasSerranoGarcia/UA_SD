#include <iostream>
#include "../../shared/hcp.hpp"
#include <thread>
#include <chrono>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <boost/program_options.hpp>
#include <ninttp/endpoints.hpp>
#include <ninttp/socket/socket.hpp>
#include <ninttp/dns/dns.hpp>

#include "nap/connection.hpp"
#include "nap/reporter.hpp"
#include "ninttp/socket/internal/select_backend.hpp"

using namespace ninttp;
using namespace std::literals;
namespace po = boost::program_options;
constexpr int HCP_PORT = 8080;

namespace {

void printAddresses(const ResolvedAddresses& addresses) {
    for (const auto& endpoint : addresses.ipv4) {
        const auto address = endpoint.addressHostOrder();
        std::clog << "Central IPv4 endpoint: "
                  << ((address >> 24) & 0xff) << '.'
                  << ((address >> 16) & 0xff) << '.'
                  << ((address >> 8) & 0xff) << '.'
                  << (address & 0xff) << ':' << endpoint.portHostOrder() << '\n';
    }
    for (const auto& endpoint : addresses.ipv6) {
        const auto bytes = endpoint.addressBytes();
        std::clog << "Central IPv6 endpoint: [";
        const auto flags = std::clog.flags();
        std::clog << std::hex;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            if (i != 0) std::clog << ':';
            std::clog << ((static_cast<unsigned int>(bytes[i]) << 8)
                         | static_cast<unsigned int>(bytes[i + 1]));
        }
        std::clog.flags(flags);
        std::clog << "]:" << endpoint.portHostOrder() << '\n';
    }
}

bool registerAndActivate(const IPv4Endpoint& centralEndpoint, const std::string& wsId,
                         std::string_view location, std::chrono::milliseconds timeout) {
    nap::Connection<IPv4Endpoint> conn(timeout);

    //TODO: if opening fails we should try with the rest of the endpoints
    auto opened = conn.open(centralEndpoint);
    if (!opened) {
        std::cerr << "Error: connecting to central failed. Reason: "
                  << opened.error().message() << std::endl;
        return false;
    }

    auto registered = conn.sendRegistration(wsId, location);
    if (!registered) {
        std::cerr << "Error: sending registration failed. Reason: "
                  << registered.error().message() << std::endl;
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
        std::cerr << "Error: sending activation failed. Reason: "
                  << activated.error().message() << std::endl;
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

std::string session(const IPv4Endpoint& engineEndpoint, const std::string& wsId,
                    std::chrono::milliseconds timeout, nap::Reporter& reporter) {
    //send ID. If any of the setup fails return the error (which will be notified to central on the outer loop
    //and then retried here again).
    StreamSocket<IPv4Endpoint> socket(Protocol::Tcp, false);
    if (auto result = hcp::connectWithin(socket, engineEndpoint, deadline(timeout)); !result)
        return "connect: " + result.error().message();
    if (auto result = hcp::sendExact(socket, "ID " + wsId + "\n", deadline(timeout)); !result)
        return "identification send: " + result.error().message();
    std::array<char, 2> response;
    if (auto result = hcp::receiveExact(socket, response, deadline(timeout)); !result)
        return "identification response: " + result.error().message();
    if (std::string_view(response.data(), response.size()) != hcp::okMsg)
        return "Engine rejected or returned an invalid WS-ID acknowledgement";

    //status checks every second
    while (true) {
        std::this_thread::sleep_for(1s);
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

int main(int argc, char* argv[]) {
    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("ws-id", po::value<std::string>()->required(), "Watering station ID (required; 1-64 letters, digits, hyphens or underscores)")
    ("engine-ip", po::value<std::string>()->required(), "Engine IPv4 address in dotted-decimal format (required)")
    ("engine-port", po::value<int>()->default_value(HCP_PORT), "Engine HCP port (1-65535)")
    ("hcp-timeout-ms", po::value<int>()->default_value(2000), "Connect/send/response deadline in milliseconds")
    ("hcp-retry-ms", po::value<int>()->default_value(1000), "Delay between connection attempts in milliseconds")
    ("nap-interval-ms", po::value<int>()->default_value(1000), "NAP OK reporting and KO retry interval in milliseconds")
    ("nap-timeout-ms", po::value<int>()->default_value(2000), "NAP connect/send/response deadline in milliseconds")
    ("central-hostname", po::value<std::string>()->required(), "Central hostname (required)")
    ("central-port", po::value<int>()->required(), "Central port (required)")
    //TODO: location should not be defaulted
    ("monitor-location", po::value<std::string>()->default_value("spain"), "Location of the station");

    po::variables_map vm;
    //stores in the variables map 'vm' as a table, the parsed options from the command line options
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }
        po::notify(vm);
    } catch (const po::error& error) {
        std::cerr << "Error: " << error.what() << "\n" << desc << "\n";
        return 1;
    }

    const auto wsId = vm["ws-id"].as<std::string>();
    if (!hcp::validId(wsId)) {
        std::cerr << "Error: --ws-id must contain 1-64 letters, digits, hyphens or underscores\n";
        return 1;
    }

    const std::string_view engineIp = vm["engine-ip"].as<std::string>();

    const int enginePort = vm["engine-port"].as<int>();
    if (enginePort < 1 || enginePort > 65535) {
        std::cerr << "Error: engine-port must be between 1 and 65535\n";
        return 1;
    }

    const auto engineEndpoint = IPv4Endpoint::fromString(
        engineIp, static_cast<std::uint16_t>(enginePort));
    if (!engineEndpoint) {
        std::cerr << "Error: engine-ip must be a valid dotted-decimal IPv4 address\n";
        return 1;
    }

    if (vm["central-hostname"].as<std::string>().empty()) {
        std::cerr << "Error: central-hostname must not be empty\n";
        return 1;
    }
    const int centralPort = vm["central-port"].as<int>();
    if (centralPort < 1 || centralPort > 65535) {
        std::cerr << "Error: central-port must be between 1 and 65535\n";
        return 1;
    }
    auto centralHostname = vm["central-hostname"].as<std::string>();
    //TODO: this resolution should be done each time we want to open a connection to central, because this resolution is dynamic.
    auto addresses = dns::resolve(centralHostname, static_cast<std::uint16_t>(centralPort));
    if (!addresses) {
        std::cerr << "Error: resolving central hostname failed. Reason: "
                    << internal::SelectedBackend::getMsgFromError(addresses.error()) << std::endl;
        return 1;
    }

    printAddresses(*addresses);

    //TODO: This could return more than one IPv4 and IPv6, which means that we would need to check for
    //all of them, searching for the one that is healthy.
    if (addresses->ipv4.empty()) {
        std::cerr << "Error: central hostname resolved to no IPv4 endpoints\n";
        return 1;
    }
    IPv4Endpoint centralEndpoint = addresses.value().ipv4[0];

    const int timeoutMs = vm["hcp-timeout-ms"].as<int>();
    const int retryMs = vm["hcp-retry-ms"].as<int>();
    if (timeoutMs <= 0 || retryMs <= 0) {
        std::cerr << "Error: HCP timeout and retry delay must be positive\n";
        return 1;
    }

    const int napIntervalMs = vm["nap-interval-ms"].as<int>();
    const int napTimeoutMs = vm["nap-timeout-ms"].as<int>();
    if (napIntervalMs <= 0 || napTimeoutMs <= 0) {
        std::cerr << "Error: NAP interval and timeout must be positive\n";
        return 1;
    }

    //TODO: add retry policy
    if (!registerAndActivate(centralEndpoint, wsId, vm["monitor-location"].as<std::string>(),
                             std::chrono::milliseconds{napTimeoutMs}))
        return 1;

    const auto timeout = std::chrono::milliseconds{timeoutMs};
    nap::Reporter reporter(centralEndpoint, wsId, std::chrono::milliseconds{napIntervalMs},
                           std::chrono::milliseconds{napTimeoutMs});
    while (true) {
        std::string failure;
        try {
            failure = session(*engineEndpoint, wsId, timeout, reporter);
        } catch (const ninttp::SocketError& error) {
            failure = error.what();
        }
        // session's socket is destroyed before retrying: discard partial/late replies.
        std::cerr << failure << std::endl;
        reporter.update(false, failure);
        std::this_thread::sleep_for(std::chrono::milliseconds{retryMs});
    }
}
