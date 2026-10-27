#include "process_support.hpp"

#include "paths.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>

namespace {
namespace ts = testSupport;

void acknowledgeNap(ts::Tcp::socket &socket, unsigned int request) {
    const std::string body = "NAP#1.0#" + std::to_string(request) + "#3#station-1";
    unsigned char checksum = 0;
    for (unsigned char byte : body)
        checksum ^= byte;
    constexpr char hex[] = "0123456789ABCDEF";
    ts::send(socket, body + '#' + hex[checksum >> 4] + hex[checksum & 15] + "#FIN");
}

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
