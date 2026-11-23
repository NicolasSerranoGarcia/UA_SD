#include "app/options.hpp"

#include <boost/program_options.hpp>
#include <boost/url/parse.hpp>

#include <cstdlib>
#include <ostream>
#include <string_view>
#include <vector>

namespace app {
namespace po = boost::program_options;
namespace {
constexpr int WEATHER_PORT = 8080;

std::string environmentOption(const std::string &name) {
    if (name == "CENTRAL_API_URL")
        return "central-api-url";
    if (name == "WM_W_PORT")
        return "port";
    if (name == "WM_W_CONFIG_FILE")
        return "config-file";
    return {};
}
}

std::expected<std::optional<Options>, std::string>
parseOptions(int argc, char *argv[], std::ostream &output) {
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "Show available options and exit")
        ("check-config", "Validate configuration and exit")
        ("central-api-url", po::value<std::string>(),
         "Central HTTP(S) base URL (environment: CENTRAL_API_URL)")
        ("port", po::value<int>()->default_value(WEATHER_PORT),
         "Weather HTTP port (environment: WM_W_PORT)")
        ("config-file", po::value<std::string>()->default_value("config/cities.json"),
         "City configuration JSON file (environment: WM_W_CONFIG_FILE)");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            output << desc << "\nSet OPENWEATHER_API_KEY in the environment.\n";
            return std::optional<Options>{};
        }

        auto environment = po::parse_environment(desc, environmentOption);
        std::erase_if(environment.options, [](const auto &option) {
            return option.value.size() == 1 && option.value.front().empty();
        });
        po::store(environment, vm);
        po::notify(vm);
    } catch (const po::error &error) {
        return std::unexpected(std::string{error.what()});
    }

    const auto *rawKey = std::getenv("OPENWEATHER_API_KEY");
    const std::string_view key = rawKey ? std::string_view{rawKey} : std::string_view{};
    if (key.find_first_not_of(" \t\r\n") == std::string_view::npos)
        return std::unexpected("Set OPENWEATHER_API_KEY before starting WM_W.");

    const auto centralUrl = vm.count("central-api-url") ? vm["central-api-url"].as<std::string>() : "";
    if (centralUrl.find_first_not_of(" \t\r\n") == std::string::npos)
        return std::unexpected("Set CENTRAL_API_URL or pass --central-api-url before starting WM_W.");
    const auto url = boost::urls::parse_uri(centralUrl);
    if (!url || (url->scheme_id() != boost::urls::scheme::http && url->scheme_id() != boost::urls::scheme::https) ||
        url->encoded_host().empty() || (url->has_port() && url->port_number() == 0) ||
        url->has_userinfo() || url->has_query() || url->has_fragment())
        return std::unexpected("CENTRAL_API_URL must be an HTTP or HTTPS base URL with no credentials, query or fragment.");

    const int port = vm["port"].as<int>();
    if (port < 1 || port > 65535)
        return std::unexpected("WM_W_PORT must be a whole number from 1 to 65535 (also applies to --port).");

    return Options{std::string{key}, centralUrl, static_cast<std::uint16_t>(port),
                   std::filesystem::path{vm["config-file"].as<std::string>()}};
}

} // namespace app
