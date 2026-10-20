#include "process_support.hpp"

#include "paths.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>

namespace {
namespace ts = testSupport;

TEST(Process, EngineBindFailureExitsWithOpenStdin) {
    ts::Server occupied;
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
    ts::send(retry, "ID station-2\nHC");
    EXPECT_EQ(ts::receive(retry, 4), "OKOK");
}

TEST(Process, MonitorRegistersActivatesAndReportsEngineHealth) {
    ts::Engine engine;
    engine.socket().close(); // Release the initial probe session for Monitor.
    ts::Server central;
    ts::Process monitor(ts::monitorPath, ts::monitorArguments(central.port(), engine.port()));
    {
        auto registration = central.accept();
        ASSERT_EQ(ts::receiveFrame(registration), "NAP#1.0#1#0#station-1#spain#51#FIN");
        ts::send(registration, "NAP#1.0#1#3#station-1#14#FIN");
        ASSERT_EQ(ts::receiveFrame(registration), "NAP#1.0#2#1#station-1#15#FIN");
        ts::send(registration, "NAP#1.0#2#3#station-1#17#FIN");
    }
    {
        auto status = central.accept();
        ASSERT_EQ(ts::receiveFrame(status), "NAP#1.0#1#2#station-1#OK#32#FIN");
        ts::send(status, "NAP#1.0#1#3#station-1#14#FIN");
    }
    engine.process().stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    while (std::chrono::steady_clock::now() < deadline) {
        auto status = central.accept();
        const auto frame = ts::receiveFrame(status);
        ASSERT_TRUE(frame == "NAP#1.0#1#2#station-1#OK#32#FIN" ||
                    frame == "NAP#1.0#1#2#station-1#KO#32#FIN")
            << frame;
        ts::send(status, "NAP#1.0#1#3#station-1#14#FIN");
        if (frame == "NAP#1.0#1#2#station-1#KO#32#FIN")
            return;
    }
    FAIL() << "Monitor did not report the engine disconnection";
}

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
