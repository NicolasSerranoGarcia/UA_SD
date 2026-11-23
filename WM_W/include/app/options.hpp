#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace app {

struct Options {
    std::string openWeatherApiKey;
    std::string centralApiUrl;
    std::uint16_t port;
    std::filesystem::path cityFile;
    std::string openWeatherApiUrl;
    bool checkConfig;
};

// An empty optional means --help was printed; an unexpected value is a startup error.
[[nodiscard]] std::expected<std::optional<Options>, std::string>
parseOptions(int argc, char *argv[], std::ostream &output);

} // namespace app
