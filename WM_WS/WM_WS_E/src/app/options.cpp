#include "app/options.hpp"

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
    desc.add_options()("help", "Show available options and exit")(
        "queue", po::value<std::string>(),
        "Set the Kafka broker endpoint address (usually hostname for TCP proxy)")(
        "hcp-port", po::value<int>()->default_value(HCP_PORT),
        "Port where Monitor connects to Engine (1-65535)")("hcp-timeout-ms",
                                              po::value<int>()->default_value(2000),
                                              "Maximum wait for a Monitor message, in milliseconds")(
        "state-file", po::value<std::string>()->default_value("state/wm-ws-state.jsonl"),
        "Save station recovery data here (use a separate path for each station)")(
        "log-file", po::value<std::string>()->default_value("log/wm-ws-engine.log"),
        "Save activity logs here; serious errors also appear on screen");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error &error) {
        return std::unexpected(std::string{error.what()});
    }

    if (vm.count("help")) {
        output << desc << "\n";
        return std::optional<Options>{};
    }

    const std::string_view queueEndpoint =
        vm.count("queue") ? std::string_view{vm["queue"].as<std::string>()} : "";

    if (queueEndpoint.empty()) {
        return std::unexpected(std::string{"missing queue endpoint argument"});
    }

    const int hcpPort = vm["hcp-port"].as<int>();
    if (hcpPort < 1 || hcpPort > 65535) {
        return std::unexpected(std::string{"hcp-port must be between 1 and 65535"});
    }

    const int timeoutMs = vm["hcp-timeout-ms"].as<int>();
    if (timeoutMs <= 0) {
        return std::unexpected(std::string{"HCP timeout must be positive"});
    }
    const auto timeout = std::chrono::milliseconds{timeoutMs};

    const auto stateFile = vm["state-file"].as<std::string>();
    if (stateFile.empty())
        return std::unexpected(std::string{"state-file must not be empty"});
    const auto logFile = vm["log-file"].as<std::string>();
    if (logFile.empty())
        return std::unexpected(std::string{"log-file must not be empty"});
    return Options{std::string{queueEndpoint}, static_cast<std::uint16_t>(hcpPort), timeout, stateFile, logFile};
}

} // namespace app
