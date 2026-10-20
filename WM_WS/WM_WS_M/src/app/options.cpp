#include "app/options.hpp"

#include "hcp.hpp"
#include <boost/program_options.hpp>
#include <ostream>
#include <string_view>

namespace app {
namespace po = boost::program_options;
namespace {
constexpr int HCP_PORT = 8080;
}

std::expected<std::optional<Options>, std::string> parseOptions(int argc, char *argv[],
                                                                std::ostream &output) {
    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")(
        "ws-id", po::value<std::string>()->required(),
        "Watering station ID (required; 1-64 letters, digits, hyphens or underscores)")(
        "engine-ip", po::value<std::string>()->required(),
        "Engine IPv4 address in dotted-decimal format (required)")(
        "engine-port", po::value<int>()->default_value(HCP_PORT),
        "Engine HCP port (1-65535)")("hcp-timeout-ms", po::value<int>()->default_value(2000),
                                     "Connect/send/response deadline in milliseconds")(
        "hcp-retry-ms", po::value<int>()->default_value(1000),
        "Delay between connection attempts in milliseconds")(
        "nap-interval-ms", po::value<int>()->default_value(1000),
        "NAP OK reporting and KO retry interval in milliseconds")(
        "nap-timeout-ms", po::value<int>()->default_value(2000),
        "NAP connect/send/response deadline in milliseconds")(
        "central-hostname", po::value<std::string>()->required(), "Central hostname (required)")(
        "central-port", po::value<int>()->required(), "Central port (required)")(
        "monitor-location", po::value<std::string>()->default_value("spain"),
        "Location of the station");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            output << desc << "\n";
            return std::optional<Options>{};
        }
        po::notify(vm);
    } catch (const po::error &error) {
        return std::unexpected(std::string{error.what()});
    }

    const auto wsId = vm["ws-id"].as<std::string>();
    if (!hcp::validId(wsId)) {
        return std::unexpected(
            std::string{"--ws-id must contain 1-64 letters, digits, hyphens or underscores"});
    }

    const std::string_view engineIp = vm["engine-ip"].as<std::string>();

    const int enginePort = vm["engine-port"].as<int>();
    if (enginePort < 1 || enginePort > 65535) {
        return std::unexpected(std::string{"engine-port must be between 1 and 65535"});
    }

    const auto engineEndpoint =
        ninttp::IPv4Endpoint::fromString(engineIp, static_cast<std::uint16_t>(enginePort));
    if (!engineEndpoint) {
        return std::unexpected(
            std::string{"engine-ip must be a valid dotted-decimal IPv4 address"});
    }

    if (vm["central-hostname"].as<std::string>().empty()) {
        return std::unexpected(std::string{"central-hostname must not be empty"});
    }
    const int centralPort = vm["central-port"].as<int>();
    if (centralPort < 1 || centralPort > 65535) {
        return std::unexpected(std::string{"central-port must be between 1 and 65535"});
    }
    const int timeoutMs = vm["hcp-timeout-ms"].as<int>();
    const int retryMs = vm["hcp-retry-ms"].as<int>();
    if (timeoutMs <= 0 || retryMs <= 0) {
        return std::unexpected(std::string{"HCP timeout and retry delay must be positive"});
    }

    const int napIntervalMs = vm["nap-interval-ms"].as<int>();
    const int napTimeoutMs = vm["nap-timeout-ms"].as<int>();
    if (napIntervalMs <= 0 || napTimeoutMs <= 0) {
        return std::unexpected(std::string{"NAP interval and timeout must be positive"});
    }

    const auto location = vm["monitor-location"].as<std::string>();
    if (location.empty() || location.find('#') != std::string::npos) {
        return std::unexpected(std::string{"monitor-location must be nonempty and contain no #"});
    }
    return Options{wsId,
                   *engineEndpoint,
                   vm["central-hostname"].as<std::string>(),
                   static_cast<std::uint16_t>(centralPort),
                   location,
                   std::chrono::milliseconds{timeoutMs},
                   std::chrono::milliseconds{retryMs},
                   std::chrono::milliseconds{napIntervalMs},
                   std::chrono::milliseconds{napTimeoutMs}};
}

} // namespace app
