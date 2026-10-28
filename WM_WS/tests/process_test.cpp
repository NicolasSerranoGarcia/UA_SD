#include "process_support.hpp"

#include "paths.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>

namespace {
namespace ts = testSupport;

struct TemporaryLog {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("wm-terminal-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    ~TemporaryLog() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    std::string read() const {
        std::ifstream stream(path);
        return {std::istreambuf_iterator<char>(stream), {}};
    }
};

struct TemporaryState {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("wm-recovery-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryState() {
        std::error_code ignored;
        std::filesystem::remove(path.string() + ".0", ignored);
        std::filesystem::remove(path.string() + ".1", ignored);
    }
};

TEST(Process, EngineRecoversExposedMeasurementsAfterHardKill) {
    TemporaryLog log;
    TemporaryState state;
    const std::vector<std::string> arguments{"--log-file", log.path.string(), "--state-file", state.path.string()};
    double water = 0, elapsed = 0;
    {
        ts::Engine station(arguments, true);
        ts::send(station.socket(), "ID 1\nAC");
        ASSERT_EQ(ts::receive(station.socket(), 4), "OKOK");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
        while (!std::filesystem::exists(state.path.string() + ".1")) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            (void)station.process().waitFor(std::chrono::milliseconds{20});
        }
        station.process().sendInput("start\n");
        const std::regex measurement{R"(Water delivered: ([0-9.]+) L \| Watering time: ([0-9.]+) s)"};
        while (elapsed < 0.5) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            EXPECT_FALSE(station.process().waitFor(std::chrono::milliseconds{50}));
            ts::send(station.socket(), "HC");
            ASSERT_EQ(ts::receive(station.socket(), 2), "OK");
            const auto &output = station.process().output();
            for (std::sregex_iterator entry(output.begin(), output.end(), measurement), end; entry != end; ++entry) {
                water = std::stod((*entry)[1]);
                elapsed = std::stod((*entry)[2]);
            }
        }
        station.process().stop(); // Abrupt process termination, without the quit/save path.
    }
    ts::Engine recovered(arguments);
    ts::send(recovered.socket(), "ID 1\n");
    ASSERT_EQ(ts::receive(recovered.socket(), 2), "OK");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    bool found = false;
    while (!found && std::chrono::steady_clock::now() < deadline) {
        for (const auto *suffix : {".0", ".1"}) {
            std::ifstream file(state.path.string() + suffix);
            const std::string contents{std::istreambuf_iterator<char>(file), {}};
            if (contents.empty() || contents.back() != '}' || contents.find("ws-finish-conf") == std::string::npos) continue;
            std::smatch liters, time;
            if (!std::regex_search(contents, liters, std::regex{R"("total_liters":([0-9.eE+-]+))"}) ||
                !std::regex_search(contents, time, std::regex{R"("total_time":([0-9.eE+-]+))"})) continue;
            EXPECT_GE(std::stod(liters[1]) + 0.0051, water);
            EXPECT_GE(std::stod(time[1]) + 0.0051, elapsed);
            found = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    EXPECT_TRUE(found) << "Recovered final summary did not retain the displayed measurements";
}

TEST(Process, CentralDownAcknowledgementDoesNotConsumeLeakNotification) {
    ts::Engine station;
    ts::send(station.socket(), "ID station-1\nAC");
    ASSERT_EQ(ts::receive(station.socket(), 4), "OKOK");
    station.process().sendInput("fault\nrepair\n");
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    ts::send(station.socket(), "DCHC");
    EXPECT_EQ(ts::receive(station.socket(), 4), "OKKO");
    ts::send(station.socket(), "HC");
    EXPECT_EQ(ts::receive(station.socket(), 2), "OK");
}

TEST(Process, ASecondConnectionCannotDisplaceHealthyMonitor) {
    ts::Engine station;
    ts::send(station.socket(), "ID station-1\nHC");
    ASSERT_EQ(ts::receive(station.socket(), 4), "OKOK");
    auto other = station.reconnect();
    ts::send(other, "ID another-station\n");
    station.process().sendInput("status\n");
    ts::send(station.socket(), "HC");
    EXPECT_EQ(ts::receive(station.socket(), 2), "OK");
    EXPECT_TRUE(station.process().running());
}

TEST(Process, EngineMenuKeepsRoutineLogsInFileAndQuitsWithOpenStdin) {
    TemporaryLog log;
    std::uint16_t port;
    { ts::Server reservation; port = reservation.port(); }
    ts::Process engine(ts::enginePath, {"--queue", "127.0.0.1:9092", "--hcp-port",
        std::to_string(port), "--log-file", log.path.string()}, true);
    engine.sendInput("help\nstatus\nquit\n");
    const auto result = engine.waitFor(std::chrono::seconds{5});
    ASSERT_TRUE(result) << engine.errors();
    EXPECT_EQ(*result, 0) << engine.errors();
    for (const auto *action : {"Start irrigation", "Stop irrigation", "Simulate a fault", "Repair the fault", "Status", "Quit"})
        EXPECT_NE(engine.output().find(action), std::string::npos) << action;
    EXPECT_EQ(engine.output().find("[Engine][Kafka]"), std::string::npos);
    EXPECT_NE(log.read().find("[Engine][Kafka]"), std::string::npos);
}

TEST(Process, EngineRefreshesWaterMeasurementsWithoutAnotherCommand) {
    TemporaryLog log;
    const auto state = log.path.string() + ".state";
    struct Cleanup {
        std::string path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path + ".0", ignored);
            std::filesystem::remove(path + ".1", ignored);
        }
    } cleanup{state};
    std::uint16_t port;
    { ts::Server reservation; port = reservation.port(); }
    ts::Process engine(ts::enginePath, {"--queue", "127.0.0.1:9092", "--hcp-port",
        std::to_string(port), "--log-file", log.path.string(), "--state-file", state}, true);
    boost::asio::io_context context;
    ts::Tcp::socket health(context);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    for (;;) {
        boost::system::error_code error;
        health.connect({boost::asio::ip::address_v4::loopback(), port}, error);
        if (!error)
            break;
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        health.close();
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    health.non_blocking(true);
    ts::send(health, "ID 1\nAC");
    ASSERT_EQ(ts::receive(health, 4), "OKOK");
    while (!std::filesystem::exists(state + ".1")) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    engine.sendInput("start\n");
    for (int count = 0; count < 5; ++count) {
        std::this_thread::sleep_for(std::chrono::milliseconds{600});
        ts::send(health, "HC");
        ASSERT_EQ(ts::receive(health, 2), "OK");
    }
    health.close();
    engine.sendInput("quit\n");
    const auto result = engine.waitFor(std::chrono::seconds{5});
    ASSERT_TRUE(result) << engine.errors();
    EXPECT_EQ(*result, 0) << engine.errors();
    const std::regex measurement{R"(Water delivered: ([0-9.]+) L \| Watering time: ([0-9.]+) s)"};
    double previousTime = 0;
    double previousWater = 0;
    int updates = 0;
    const auto &output = engine.output();
    for (std::sregex_iterator entry(output.begin(), output.end(), measurement), end; entry != end; ++entry) {
        const double water = std::stod((*entry)[1].str());
        const double time = std::stod((*entry)[2].str());
        if (time > previousTime + 0.5 && water > previousWater) {
            ++updates;
            previousTime = time;
            previousWater = water;
        }
    }
    EXPECT_GE(updates, 2) << output;
    const std::regex flow{R"(Flow: ([0-9.]+) L/min)"};
    std::set<std::string> wateringFlows;
    for (std::sregex_iterator entry(output.begin(), output.end(), flow), end; entry != end; ++entry)
        if (std::stod((*entry)[1].str()) > 0)
            wateringFlows.insert((*entry)[1].str());
    EXPECT_GT(wateringFlows.size(), 1u) << output;
    const std::regex menu{R"(Watering station \| Engine)"};
    EXPECT_GT(std::distance(std::sregex_iterator(output.begin(), output.end(), measurement), std::sregex_iterator{}),
              std::distance(std::sregex_iterator(output.begin(), output.end(), menu), std::sregex_iterator{}));
    EXPECT_EQ(output.find('\x1b'), std::string::npos); // Pipes receive plain text.
}

TEST(Process, MonitorMenuIsVisibleAndCanQuitDuringStartup) {
    TemporaryLog log;
    ts::Server central;
    auto arguments = ts::monitorArguments(central.port());
    arguments.insert(arguments.end(), {"--nap-timeout-ms", "100", "--log-file", log.path.string()});
    ts::Process monitor(ts::monitorPath, arguments, true);
    monitor.sendInput("help\nstatus\nquit\n");
    const auto result = monitor.waitFor(std::chrono::seconds{5});
    ASSERT_TRUE(result) << monitor.errors();
    EXPECT_EQ(*result, 0) << monitor.errors();
    for (const auto *action : {"Status", "Help", "Quit"})
        EXPECT_NE(monitor.output().find(action), std::string::npos) << action;
    EXPECT_EQ(monitor.output().find("[Central][DNS]"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(log.path));
    EXPECT_NE(monitor.output().find("Central connection:"), std::string::npos);
}

TEST(Process, FatalBindErrorIsActionableOnTerminalAndRecordedInFile) {
    TemporaryLog log;
    ts::Server occupied(true);
    ts::Process engine(ts::enginePath, {"--queue", "127.0.0.1:9092", "--hcp-port",
        std::to_string(occupied.port()), "--log-file", log.path.string()}, true);
    const auto result = engine.waitFor(std::chrono::seconds{5});
    ASSERT_TRUE(result) << engine.errors();
    EXPECT_EQ(*result, 1);
    EXPECT_NE(engine.errors().find("FATAL"), std::string::npos);
    EXPECT_NE(engine.errors().find("HCP"), std::string::npos);
    EXPECT_NE(engine.errors().find("Next step:"), std::string::npos);
    EXPECT_NE(log.read().find("HCP bind"), std::string::npos);
}

void acknowledgeNap(ts::Tcp::socket &socket, unsigned int request, std::string_view id = "station-1") {
    const std::string body = "NAP#1.0#" + std::to_string(request) + "#3#" + std::string(id);
    unsigned char checksum = 0;
    for (unsigned char byte : body)
        checksum ^= byte;
    constexpr char hex[] = "0123456789ABCDEF";
    ts::send(socket, body + '#' + hex[checksum >> 4] + hex[checksum & 15] + "#FIN");
}

class CentralOutageState : public testing::TestWithParam<bool> {};

TEST_P(CentralOutageState, RecoversWithoutRestartingWs) {
    boost::asio::io_context idleContext;
    TemporaryLog log;
    TemporaryState state;
    const bool blocked = GetParam();
    if (blocked) {
        std::ofstream file(state.path.string() + ".1");
        file << R"({"revision":1,"state":{"version":1,"station_id":"1","blocked":true,"pending":[],"replies":{},"commands":{},"session":null,"intent":null,"volume_l":0,"elapsed_s":0}})";
    }
    ts::Engine station({"--log-file", log.path.string(), "--state-file", state.path.string()}, true);
    station.socket().close();
    auto central = std::make_unique<ts::Server>();
    const auto port = central->port();
    auto arguments = ts::monitorArguments(port, station.port());
    arguments[1] = "1";
    arguments.insert(arguments.end(), {"--nap-timeout-ms", "200", "--hcp-retry-ms", "100"});
    ts::Process monitor(ts::monitorPath, arguments);
    {
        auto registration = central->accept();
        ASSERT_NE(ts::receiveFrame(registration).find("#0#1#spain#"), std::string::npos);
        acknowledgeNap(registration, 1, "1");
    }
    {
        auto activation = central->accept();
        ASSERT_NE(ts::receiveFrame(activation).find("#1#1#"), std::string::npos);
        acknowledgeNap(activation, 1, "1");
    }
    auto status = central->accept();
    unsigned int request = 1;
    const auto waitForText = [&](std::string_view text, bool acknowledge, std::size_t offset = 0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
        while (station.process().output().find(text, offset) == std::string::npos) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline) << station.process().output();
            if (acknowledge) {
                ASSERT_NE(ts::receiveFrame(status).find("#2#1#OK#"), std::string::npos);
                acknowledgeNap(status, request++, "1");
            }
            EXPECT_FALSE(station.process().waitFor(std::chrono::milliseconds{20}));
        }
    };
    ASSERT_NO_FATAL_FAILURE(waitForText(blocked ? "Central connection: Available" : "Ready to water", true));
    if (!blocked) {
        station.process().sendInput("start\n");
        ASSERT_NO_FATAL_FAILURE(waitForText("Irrigation has started.", true));
    }
    const auto outageOffset = station.process().output().size();
    status.close();
    status = ts::Tcp::socket(idleContext);
    central.reset();
    ASSERT_NO_FATAL_FAILURE(waitForText(blocked ? "Out of Service - blocked by Central; Central is unavailable" :
                "Watering in progress. Central is unavailable", false, outageOffset));
    station.process().sendInput("stop\nstart\n");
    ASSERT_NO_FATAL_FAILURE(waitForText("Irrigation could not start.", false));
    const auto &output = station.process().output();
    if (blocked) EXPECT_EQ(output.find("Irrigation has started."), std::string::npos);
    else EXPECT_EQ(output.find("Irrigation has started.", output.find("Irrigation has started.") + 1), std::string::npos);
    central = std::make_unique<ts::Server>(false, port);
    {
        auto activation = central->accept();
        ASSERT_NE(ts::receiveFrame(activation).find("#1#1#"), std::string::npos);
        acknowledgeNap(activation, 1, "1");
    }
    status = central->accept();
    request = 1;
    // Observe a fresh Ready state after the outage rather than the earlier startup text.
    const auto offset = station.process().output().size();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
    while (station.process().output().find(blocked ? "Central connection: Available" : "Ready to water", offset) == std::string::npos) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline) << station.process().output();
        ts::receiveFrame(status);
        acknowledgeNap(status, request++, "1");
        EXPECT_FALSE(station.process().waitFor(std::chrono::milliseconds{20}));
    }
    EXPECT_TRUE(monitor.running());
    EXPECT_TRUE(station.process().running());
}

INSTANTIATE_TEST_SUITE_P(Availability, CentralOutageState, testing::Values(false, true));

TEST(Process, EngineBindFailureExitsWithOpenStdin) {
    ts::Server occupied(true);
    ts::Process engine(ts::enginePath, {"--queue", "127.0.0.1:9092", "--hcp-port",
                                        std::to_string(occupied.port())});
    const auto code = engine.waitFor(std::chrono::seconds{3});
    ASSERT_TRUE(code.has_value()) << "Engine hung after listener failure: " << engine.errors();
    EXPECT_EQ(*code, 1);
}

TEST(Process, EngineCommandsAndEofPreserveHealthService) {
    ts::Engine engine;
    ts::send(engine.socket(), "ID station-1\nHC");
    ASSERT_EQ(ts::receive(engine.socket(), 4), "OKOK");
    for (const auto &[command, expected] : {std::pair{"F\n", "KO"}, std::pair{"R\n", "OK"}}) {
        engine.process().sendInput(command);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (true) {
            ts::send(engine.socket(), "HC");
            const auto status = ts::receive(engine.socket(), 2);
            ASSERT_TRUE(status == "OK" || status == "KO") << status;
            if (status == expected)
                break;
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }
    engine.process().closeInput();
    ts::send(engine.socket(), "HC");
    EXPECT_EQ(ts::receive(engine.socket(), 2), "OK");
    EXPECT_TRUE(engine.process().running());
}

TEST(Process, EngineAcceptsNewSessionAfterInvalidRequest) {
    ts::Engine engine;
    ts::send(engine.socket(), "ID station-1\n");
    ASSERT_EQ(ts::receive(engine.socket(), 2), "OK");
    ts::send(engine.socket(), "XX");
    EXPECT_EQ(ts::receive(engine.socket(), 1), "");
    auto retry = engine.reconnect();
    ts::send(retry, "ID station-1\nHC");
    EXPECT_EQ(ts::receive(retry, 4), "OKOK");
}

TEST(Process, EngineRejectsIdentityChangeUntilRestart) {
    ts::Engine engine;
    ts::send(engine.socket(), "ID station-1\n");
    ASSERT_EQ(ts::receive(engine.socket(), 2), "OK");
    engine.socket().close();
    auto other = engine.reconnect();
    ts::send(other, "ID station-2\n");
    EXPECT_EQ(ts::receive(other, 2), "KO");
}

TEST(Process, HcpActivationRejectsLeakAndSucceedsAfterRepair) {
    ts::Engine engine;
    ts::send(engine.socket(), "ID station-1\nHCAC");
    ASSERT_EQ(ts::receive(engine.socket(), 6), "OKOKOK");
    engine.process().sendInput("F\n");
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    for (;;) {
        ts::send(engine.socket(), "HC");
        if (ts::receive(engine.socket(), 2) == "KO")
            break;
        ASSERT_LT(std::chrono::steady_clock::now(), limit);
    }
    ts::send(engine.socket(), "AC");
    EXPECT_EQ(ts::receive(engine.socket(), 2), "KO");
    engine.process().sendInput("R\n");
    for (;;) {
        ts::send(engine.socket(), "HC");
        if (ts::receive(engine.socket(), 2) == "OK")
            break;
        ASSERT_LT(std::chrono::steady_clock::now(), limit);
    }
    ts::send(engine.socket(), "AC");
    EXPECT_EQ(ts::receive(engine.socket(), 2), "OK");
}

class RegistrationResponse : public testing::TestWithParam<const char *> {};

TEST_P(RegistrationResponse, MonitorActivatesOnNewConnectionAndReportsEngineHealth) {
    ts::Engine engine;
    engine.socket().close(); // Release the initial probe session for Monitor.
    ts::Server central;
    ts::Process monitor(ts::monitorPath, ts::monitorArguments(central.port(), engine.port()));
    {
        auto registration = central.accept();
        ASSERT_EQ(ts::receiveFrame(registration), "NAP#1.0#1#0#station-1#spain#51#FIN");
        ts::send(registration, GetParam());
    } // Central closes the registration connection after replying.
    {
        auto activation = central.accept();
        ASSERT_EQ(ts::receiveFrame(activation), "NAP#1.0#1#1#station-1#16#FIN");
        ts::send(activation, "NAP#1.0#1#3#station-1#14#FIN");
    }
    auto status = central.accept();
    ASSERT_EQ(ts::receiveFrame(status), "NAP#1.0#1#2#station-1#OK#32#FIN");
    acknowledgeNap(status, 1);
    engine.process().stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    unsigned int request = 2;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto frame = ts::receiveFrame(status);
        ASSERT_TRUE(frame.starts_with("NAP#1.0#" + std::to_string(request) + "#2#station-1#")) << frame;
        acknowledgeNap(status, request++);
        if (frame.find("#KO#") != std::string::npos)
            return;
    }
    FAIL() << "Monitor did not report the engine disconnection";
}

TEST(Process, LeakClosesNapAndRepairRequiresNewActivation) {
    ts::Engine engine;
    engine.socket().close();
    ts::Server central;
    ts::Process monitor(ts::monitorPath, ts::monitorArguments(central.port(), engine.port()));
    {
        auto registration = central.accept();
        ASSERT_EQ(ts::receiveFrame(registration), "NAP#1.0#1#0#station-1#spain#51#FIN");
        acknowledgeNap(registration, 1);
    }
    {
        auto activation = central.accept();
        ASSERT_EQ(ts::receiveFrame(activation), "NAP#1.0#1#1#station-1#16#FIN");
        acknowledgeNap(activation, 1);
    }
    auto status = central.accept();
    ASSERT_EQ(ts::receiveFrame(status), "NAP#1.0#1#2#station-1#OK#32#FIN");
    acknowledgeNap(status, 1);
    engine.process().sendInput("F\n");
    bool reportedFault = false;
    for (unsigned int request = 2; request < 40; ++request) {
        const auto frame = ts::receiveFrame(status);
        acknowledgeNap(status, request);
        if (frame.find("#KO#") != std::string::npos) {
            reportedFault = true;
            break;
        }
    }
    ASSERT_TRUE(reportedFault);
    EXPECT_EQ(ts::receive(status, 1), "");
    engine.process().sendInput("R\n");
    {
        auto activation = central.accept();
        EXPECT_EQ(ts::receiveFrame(activation), "NAP#1.0#1#1#station-1#16#FIN");
        acknowledgeNap(activation, 1);
    }
    auto recovered = central.accept();
    EXPECT_EQ(ts::receiveFrame(recovered), "NAP#1.0#1#2#station-1#OK#32#FIN");
    acknowledgeNap(recovered, 1);
}

INSTANTIATE_TEST_SUITE_P(Nap, RegistrationResponse,
                         testing::Values("NAP#1.0#1#3#station-1#14#FIN",
                                         "NAP#1.0#1#4#1#station-1#01#FIN"));

class ReservedZeroResponse : public testing::TestWithParam<const char *> {};

TEST_P(ReservedZeroResponse, CannotAuthorizeActivation) {
    ts::Server central;
    ts::Process monitor(ts::monitorPath, ts::monitorArguments(central.port()));
    auto registration = central.accept();
    ASSERT_EQ(ts::receiveFrame(registration), "NAP#1.0#1#0#station-1#spain#51#FIN");
    ts::send(registration, GetParam());
    EXPECT_EQ(ts::receive(registration, 1), "") << "Reserved zero authorized activation";
    const auto code = monitor.waitFor(std::chrono::seconds{3});
    ASSERT_TRUE(code.has_value()) << monitor.errors();
    EXPECT_EQ(*code, 1);
}

INSTANTIATE_TEST_SUITE_P(Nap, ReservedZeroResponse,
                         testing::Values("NAP#1.0#0#3#station-1#15#FIN",
                                         "NAP#1.0#0#4#0#station-1#01#FIN"));

std::vector<std::string> invalidCentralArguments() {
    return {"--ws-id",
            "station-1",
            "--engine-ip",
            "127.0.0.1",
            "--central-hostname",
            "does-not-exist.invalid",
            "--central-port",
            "9000"};
}

TEST(Process, MonitorValidatesTimingBeforeDns) {
    auto arguments = invalidCentralArguments();
    arguments.insert(arguments.end(), {"--nap-timeout-ms", "0"});
    const auto result = ts::command(ts::monitorPath, arguments);
    EXPECT_EQ(result.code, 1);
    EXPECT_NE(result.errors.find("NAP interval and timeout must be positive"), std::string::npos);
}

TEST(Process, MonitorValidatesLocationBeforeDns) {
    auto arguments = invalidCentralArguments();
    arguments.insert(arguments.end(), {"--monitor-location", "invalid#location"});
    const auto result = ts::command(ts::monitorPath, arguments);
    EXPECT_EQ(result.code, 1);
    EXPECT_NE(result.errors.find("monitor-location"), std::string::npos);
}

TEST(Process, HelpNeedsNoRequiredOptions) {
    for (const auto &binary : {std::string{ts::enginePath}, std::string{ts::monitorPath}}) {
        SCOPED_TRACE(binary);
        const auto result = ts::command(binary, {"--help"});
        EXPECT_EQ(result.code, 0);
        EXPECT_NE(result.output.find("Allowed options"), std::string::npos);
    }
}

} // namespace
