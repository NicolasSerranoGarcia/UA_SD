#include "app/options.hpp"

#include "hcp.hpp"
#include <algorithm>
#include <boost/program_options.hpp>
#include <boost/url/parse.hpp>
#include <charconv>
#include <ostream>
#include <string_view>

namespace app {
namespace po = boost::program_options;
namespace {
constexpr int HCP_PORT = 8080;
constexpr auto DEFAULT_REGISTRY_URL = "https://uasd-production-752a45b0.up.railway.app";
}

std::expected<std::optional<Options>, std::string> parseOptions(int argc, char *argv[],
                                                                std::ostream &output) {
    po::options_description desc("Allowed options");
    desc.add_options()("help", "Show available options and exit")(
        "ws-id", po::value<std::string>()->required(),
        "Watering station ID (required; 1-64 letters, digits, hyphens or underscores)")(
        "engine-ip", po::value<std::string>(),
        "Engine IPv4 address; supply this or --engine-hostname")(
        "engine-hostname", po::value<std::string>(),
        "Engine hostname, resolved again on each connection attempt")(
        "engine-port", po::value<int>()->default_value(HCP_PORT),
        "Engine connection port (1-65535)")("hcp-timeout-ms", po::value<int>()->default_value(2000),
                                     "Connect/send/response deadline in milliseconds")(
        "hcp-retry-ms", po::value<int>()->default_value(1000),
        "Delay between connection attempts in milliseconds")(
        "nap-interval-ms", po::value<int>()->default_value(1000),
        "Delay between status reports and retries to Central, in milliseconds")(
        "nap-timeout-ms", po::value<int>()->default_value(2000),
        "Maximum wait for Central to respond, in milliseconds")(
        "central-hostname", po::value<std::string>(), "Central hostname for legacy NAP")(
        "central-port", po::value<int>(), "Central port for legacy NAP")(
        "monitor-location", po::value<std::string>()->default_value("alicante"),
        "City where the station is located")(
        "log-file", po::value<std::string>()->default_value("log/wm-ws-monitor.log"),
        "Save activity logs here; serious errors also appear on screen")(
        "registry-url", po::value<std::string>()->default_value(DEFAULT_REGISTRY_URL),
        "HTTPS origin of Registry; explicit Central options select legacy NAP unless a Registry URL is supplied")(
        "registry-ca-file", po::value<std::string>()->default_value(""),
        "Trusted CA certificate file for a local Registry")(
        "registry-state-dir", po::value<std::string>()->default_value("state/registry"),
        "Directory for this station's private Registry credential");

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

    const auto &registryOption = vm["registry-url"];
    const bool registry = !registryOption.defaulted() || (!vm.count("central-hostname") && !vm.count("central-port"));
    auto registryUrl = registry ? registryOption.as<std::string>() : "";
    if (registry) {
        const auto url = boost::urls::parse_uri(registryUrl);
        if (!url || url->scheme_id() != boost::urls::scheme::https || url->encoded_host().empty() ||
            url->has_userinfo() || url->has_query() || url->has_fragment() ||
            (url->has_port() && url->port_number() == 0) ||
            (!url->encoded_path().empty() && url->encoded_path() != "/"))
            return std::unexpected("--registry-url must be an HTTPS origin with no path or credentials.");
        if (registryUrl.ends_with('/'))
            registryUrl.pop_back();
        std::int64_t id{};
        const auto end = wsId.data() + wsId.size();
        const auto [last, error] = std::from_chars(wsId.data(), end, id);
        if (error != std::errc{} || last != end || id <= 0 || wsId != std::to_string(id))
            return std::unexpected("--ws-id must be a positive decimal station ID in Registry mode.");
    }

    const auto engineAddresses = vm.count("engine-ip") + vm.count("engine-hostname");
    if (engineAddresses > 1 || (!registry && engineAddresses != 1))
        return std::unexpected(std::string{"Supply exactly one of --engine-ip or --engine-hostname"});
    const auto engineHostname = vm.count("engine-hostname") ? vm["engine-hostname"].as<std::string>() : "";
    if (vm.count("engine-hostname") && engineHostname.empty())
        return std::unexpected(std::string{"engine-hostname must not be empty"});
    const std::string engineIp = vm.count("engine-ip") ? vm["engine-ip"].as<std::string>() : "127.0.0.1";

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

    const auto centralHostname = vm.count("central-hostname") ? vm["central-hostname"].as<std::string>() : "";
    if ((!registry || vm.count("central-hostname")) && centralHostname.empty())
        return std::unexpected("Supply a nonempty --central-hostname for legacy NAP.");
    if (!registry && !vm.count("central-port"))
        return std::unexpected("Supply --central-port for legacy NAP.");
    const int centralPort = vm.count("central-port") ? vm["central-port"].as<int>() : 40931;
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
    if (registry && (location.size() > 64 || !std::ranges::all_of(location, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        })))
        return std::unexpected("--monitor-location must be a lowercase city slug in Registry mode.");
    const auto registryDir = vm["registry-state-dir"].as<std::string>();
    if (registryDir.empty())
        return std::unexpected("--registry-state-dir must not be empty.");
    const auto logFile = vm["log-file"].as<std::string>();
    if (logFile.empty())
        return std::unexpected(std::string{"log-file must not be empty"});
    return Options{wsId,
                   *engineEndpoint,
                   centralHostname,
                   static_cast<std::uint16_t>(centralPort),
                   location,
                   std::chrono::milliseconds{timeoutMs},
                   std::chrono::milliseconds{retryMs},
                   std::chrono::milliseconds{napIntervalMs},
                   std::chrono::milliseconds{napTimeoutMs}, logFile, engineHostname,
                   registryUrl, vm["registry-ca-file"].as<std::string>(), registryDir};
}

} // namespace app
