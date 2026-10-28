#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <ninttp/endpoints.hpp>
#include <optional>
#include <string>

namespace app {

struct Options {
    std::string wsId;
    ninttp::IPv4Endpoint engineEndpoint;
    std::string centralHostname;
    std::uint16_t centralPort;
    std::string location;
    std::chrono::milliseconds hcpTimeout;
    std::chrono::milliseconds hcpRetry;
    std::chrono::milliseconds napInterval;
    std::chrono::milliseconds napTimeout;
    std::string logFile = "log/wm-ws-monitor.log";
    std::string engineHostname;
};

// An empty optional means --help was printed; an unexpected value is a CLI error.
[[nodiscard]] std::expected<std::optional<Options>, std::string>
parseOptions(int argc, char *argv[], std::ostream &output);

} // namespace app
