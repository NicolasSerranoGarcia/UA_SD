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

using namespace ninttp;
using namespace std::literals;
namespace po = boost::program_options;
constexpr int HCP_PORT = 8080;

int main(int argc, char* argv[]) {
    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("ws-id", po::value<std::string>(), "Watering station ID (required; 1-64 letters, digits, hyphens or underscores)")
    ("engine-ip", po::value<std::string>(), "Engine IPv4 address in dotted-decimal format (required)")
    ("engine-port", po::value<int>()->default_value(HCP_PORT), "Engine HCP port (1-65535)")
    ("hcp-timeout-ms", po::value<int>()->default_value(2000), "Connect/send/response deadline in milliseconds")
    ("hcp-retry-ms", po::value<int>()->default_value(1000), "Delay between connection attempts in milliseconds");

    po::variables_map vm;
    //stores in the variables map 'vm' as a table, the parsed options from the command line options
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& error) {
        std::cerr << "Error: " << error.what() << "\n" << desc << "\n";
        return 1;
    }

    if(vm.count("help")){
        std::cout << desc << "\n";
        return 0;
    }

    const std::string wsId = vm.count("ws-id") ? vm["ws-id"].as<std::string>() : "";
    if (!hcp::validId(wsId)) {
        std::cerr << "Error: --ws-id is required and must contain 1-64 letters, digits, hyphens or underscores\n";
        return 1;
    }

    const std::string_view engineIp = vm.count("engine-ip")
        ? std::string_view{vm["engine-ip"].as<std::string>()} : "";

    if (engineIp.empty()) {
        std::cerr << "Error: missing engine-ip argument\n" << desc << "\n";
        return 1;
    }

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

    const int timeoutMs = vm["hcp-timeout-ms"].as<int>();
    const int retryMs = vm["hcp-retry-ms"].as<int>();
    if (timeoutMs <= 0 || retryMs <= 0) {
        std::cerr << "Error: HCP timeout and retry delay must be positive\n";
        return 1;
    }
    const auto timeout = std::chrono::milliseconds{timeoutMs};
    const auto deadline = [&] { return std::chrono::steady_clock::now() + timeout; };
    std::optional<bool> lastHealthy;
    // this is a placeholder that should report to central via NAP
    const auto report = [&](bool healthy, std::string_view reason) {
        if (!lastHealthy || *lastHealthy != healthy) {
            std::cout << "CENTRAL hook: ws=" << wsId << " state="
                      << (healthy ? "healthy" : "fault") << " reason=" << reason << std::endl;
            lastHealthy = healthy;
        }
    };
    const auto session = [&]() -> std::string {
        //send ID. If any of the setup fails return the error (which will be notified to central on the outer loop
        //and then retried here again).
        StreamSocket<IPv4Endpoint> socket(Protocol::Tcp, false);
        if (auto result = hcp::connectWithin(socket, *engineEndpoint, deadline()); !result)
            return "connect: " + result.error().message();
        if (auto result = hcp::sendExact(socket, "ID " + wsId + "\n", deadline()); !result)
            return "identification send: " + result.error().message();
        std::array<char, 2> response;
        if (auto result = hcp::receiveExact(socket, response, deadline()); !result)
            return "identification response: " + result.error().message();
        if (std::string_view(response.data(), response.size()) != hcp::okMsg)
            return "Engine rejected or returned an invalid WS-ID acknowledgement";

        //status checks every second
        while (true) {
            std::this_thread::sleep_for(1s);
            if (auto result = hcp::sendExact(socket, "HC", deadline()); !result)
                return "health request: " + result.error().message();
            if (auto result = hcp::receiveExact(socket, response, deadline()); !result)
                return "health response: " + result.error().message();
            const std::string_view status(response.data(), response.size());
            if (status != hcp::okMsg && status != hcp::koMsg)
                return "invalid HCP health response";
            std::cout << status << std::endl;
            report(status == hcp::okMsg, status);
        }
    };
    while (true) {
        std::string failure;
        try {
            failure = session();
        } catch (const ninttp::SocketError& error) {
            failure = error.what();
        }
        // session's socket is destroyed before retrying: discard partial/late replies.
        std::cerr << failure << std::endl;
        report(false, failure);
        std::this_thread::sleep_for(std::chrono::milliseconds{retryMs});
    }
}
