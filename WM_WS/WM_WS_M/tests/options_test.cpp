#include "app/options.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::expected<std::optional<app::Options>, std::string>
parse(std::vector<std::string> arguments, std::ostream &output) {
    std::vector<char *> argv;
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    return app::parseOptions(static_cast<int>(argv.size()), argv.data(), output);
}

std::vector<std::string> validArguments() {
    return {"WM_WS_M",         "--ws-id",        "station-1",
            "--engine-ip",     "127.0.0.1",      "--central-hostname",
            "central.example", "--central-port", "9000"};
}

TEST(MonitorOptions, HelpDoesNotRequireConfiguration) {
    std::ostringstream output;
    const auto result = parse({"WM_WS_M", "--help"}, output);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->has_value());
    EXPECT_NE(output.str().find("Allowed options"), std::string::npos);
}

TEST(MonitorOptions, ParsesOwnedConfigurationAndDefaultsWithoutDns) {
    std::ostringstream output;
    const auto result = parse(validArguments(), output);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    const auto &options = **result;
    EXPECT_EQ(options.wsId, "station-1");
    EXPECT_EQ(options.centralHostname, "central.example");
    EXPECT_EQ(options.centralPort, 9000);
    EXPECT_EQ(options.engineEndpoint.portHostOrder(), 8080);
    EXPECT_EQ(options.location, "spain");
    EXPECT_EQ(options.hcpTimeout, std::chrono::milliseconds{2000});
    EXPECT_EQ(options.hcpRetry, std::chrono::milliseconds{1000});
    EXPECT_EQ(options.napInterval, std::chrono::milliseconds{1000});
    EXPECT_EQ(options.napTimeout, std::chrono::milliseconds{2000});
    EXPECT_EQ(options.logFile, "log/wm-ws-monitor.log");
}

TEST(MonitorOptions, RejectsMissingRequiredOptions) {
    std::ostringstream output;
    EXPECT_FALSE(parse({"WM_WS_M"}, output).has_value());
}

class InvalidMonitorOption : public testing::TestWithParam<std::pair<std::string, std::string>> {};

TEST_P(InvalidMonitorOption, RejectsInvalidValue) {
    auto arguments = validArguments();
    const auto &[name, value] = GetParam();
    bool replaced = false;
    for (std::size_t i = 1; i + 1 < arguments.size(); i += 2) {
        if (arguments[i] == name) {
            arguments[i + 1] = value;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        arguments.push_back(name);
        arguments.push_back(value);
    }
    std::ostringstream output;
    EXPECT_FALSE(parse(std::move(arguments), output).has_value());
}

INSTANTIATE_TEST_SUITE_P(
    Validation, InvalidMonitorOption,
    testing::Values(std::pair{"--ws-id", "invalid id"}, std::pair{"--engine-ip", "999.0.0.1"},
                    std::pair{"--engine-port", "0"}, std::pair{"--central-port", "65536"},
                    std::pair{"--central-hostname", ""}, std::pair{"--hcp-timeout-ms", "0"},
                    std::pair{"--hcp-retry-ms", "-1"}, std::pair{"--nap-timeout-ms", "0"},
                    std::pair{"--nap-interval-ms", "-1"},
                    std::pair{"--monitor-location", "invalid#location"},
                    std::pair{"--monitor-location", ""}, std::pair{"--log-file", ""}));

} // namespace
