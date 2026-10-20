#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <optional>
#include <string>

namespace app {

struct Options {
    std::string queueEndpoint;
    std::uint16_t hcpPort;
    std::chrono::milliseconds hcpTimeout;
};

// An empty optional means --help was printed; an unexpected value is a CLI error.
[[nodiscard]] std::expected<std::optional<Options>, std::string>
parseOptions(int argc, char *argv[], std::ostream &output);

} // namespace app
