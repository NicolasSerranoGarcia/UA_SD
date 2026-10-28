#include "app/app.hpp"
#include "app/status.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace {

app::Options options(std::uint16_t port) {
    using namespace std::chrono_literals;
    return {"station-1", ninttp::IPv4Endpoint::loopback(8080), "127.0.0.1", port,
            "spain", 100ms, 100ms, 100ms, 100ms};
}

TEST(MonitorApp, StopBeforeStartupSkipsNetworkAndReportsStopped) {
    std::stop_source stop;
    stop.request_stop();
    std::vector<app::Status> updates;
    auto configuration = options(1);
    configuration.centralHostname = "unused.invalid";
    EXPECT_EQ(app::run(configuration, stop.get_token(),
                       [&](const app::Status &status) { updates.push_back(status); }), 0);
    ASSERT_EQ(updates.size(), 1);
    EXPECT_EQ(updates.back().phase, app::Phase::Stopped);
    EXPECT_FALSE(updates.back().centralEndpoint.has_value());
}

TEST(MonitorApp, StartupOutagePublishesErrorAndRetriesUntilStopped) {
    boost::asio::io_context context;
    using Tcp = boost::asio::ip::tcp;
    // Reserve a local port without listening, so no other process can accept it.
    Tcp::socket reserved(context);
    reserved.open(Tcp::v4());
    reserved.bind(Tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    const auto port = reserved.local_endpoint().port();
    std::vector<app::Status> updates;
    std::stop_source stop;
    std::jthread watchdog([&](std::stop_token done) {
        for (int i = 0; i < 300 && !done.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        if (!done.stop_requested()) stop.request_stop();
    });
    bool observed = false;
    EXPECT_EQ(app::run(options(port), stop.get_token(), [&](const app::Status &status) {
        updates.push_back(status);
        if (!status.centralError.empty()) { observed = true; stop.request_stop(); }
    }), 0);
    watchdog.request_stop();
    EXPECT_TRUE(observed);
    EXPECT_EQ(updates.front().phase, app::Phase::Resolving);
    EXPECT_EQ(updates.back().phase, app::Phase::Stopped);
    ASSERT_TRUE(updates.back().centralEndpoint);
    EXPECT_EQ(updates.back().centralEndpoint->portHostOrder(), port);
    EXPECT_FALSE(updates.back().centralAvailable);
    EXPECT_FALSE(updates.back().centralError.empty());
}

TEST(MonitorApp, ReportsHealthAndDisconnectAndStopsWithoutWaitingForRetry) {
    using namespace std::chrono_literals;
    using Tcp = boost::asio::ip::tcp;
    boost::asio::io_context context;
    Tcp::acceptor central(context, Tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    Tcp::acceptor engine(context, Tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    auto configuration = options(central.local_endpoint().port());
    configuration.engineEndpoint = ninttp::IPv4Endpoint::loopback(engine.local_endpoint().port());
    configuration.hcpTimeout = 500ms;
    configuration.napTimeout = 500ms;
    configuration.hcpRetry = 30s;

    std::stop_source stop;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::jthread watchdog([&](std::stop_token done) {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, done, 6s, [] { return false; });
        if (!done.stop_requested())
            stop.request_stop();
    });
    const auto accept = [&](Tcp::acceptor &listener) {
        listener.non_blocking(true);
        Tcp::socket peer(context);
        const auto limit = std::chrono::steady_clock::now() + 5s;
        for (;;) {
            boost::system::error_code error;
            listener.accept(peer, error);
            if (!error) {
                peer.non_blocking(true);
                return peer;
            }
            if (error != boost::asio::error::would_block || std::chrono::steady_clock::now() >= limit)
                throw boost::system::system_error(error);
            std::this_thread::sleep_for(1ms);
        }
    };
    const auto receive = [](Tcp::socket &peer, std::string_view terminator) {
        std::string message;
        const auto limit = std::chrono::steady_clock::now() + 5s;
        while (!message.ends_with(terminator)) {
            char byte;
            boost::system::error_code error;
            if (peer.read_some(boost::asio::buffer(&byte, 1), error))
                message += byte;
            else if (error != boost::asio::error::would_block || std::chrono::steady_clock::now() >= limit)
                throw boost::system::system_error(error);
            else
                std::this_thread::sleep_for(1ms);
        }
        return message;
    };
    std::exception_ptr centralError, engineError;
    std::jthread centralPeer([&] {
        try {
            for (int request = 0; request != 2; ++request) {
                auto peer = accept(central);
                receive(peer, "#FIN");
                constexpr std::string_view response = "NAP#1.0#1#3#station-1#14#FIN";
                boost::asio::write(peer, boost::asio::buffer(response));
            }
        } catch (...) { centralError = std::current_exception(); }
    });
    std::jthread enginePeer([&] {
        try {
            auto peer = accept(engine);
            EXPECT_EQ(receive(peer, "\n"), "ID station-1\n");
            boost::asio::write(peer, boost::asio::buffer("OK", 2));
            for (const auto *health : {"KO", "OK"}) {
                EXPECT_EQ(receive(peer, "HC"), "HC");
                boost::asio::write(peer, boost::asio::buffer(health, 2));
            }
        } catch (...) { engineError = std::current_exception(); }
    });
    std::vector<app::Status> updates;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(app::run(configuration, stop.get_token(), [&](const app::Status &status) {
        updates.push_back(status);
        if (status.phase == app::Phase::Retrying)
            stop.request_stop();
    }), 0);
    const auto duration = std::chrono::steady_clock::now() - start;
    watchdog.request_stop();
    centralPeer.join();
    enginePeer.join();
    EXPECT_FALSE(centralError);
    EXPECT_FALSE(engineError);
    EXPECT_LT(duration, 5s);
    std::vector<app::Health> responses;
    for (const auto &status : updates) {
        if (status.phase == app::Phase::Monitoring && status.lastHealth)
            responses.push_back(status.health);
    }
    EXPECT_EQ(responses, (std::vector{app::Health::Ko, app::Health::Ok}));
    ASSERT_GE(updates.size(), 2);
    EXPECT_EQ(updates[updates.size() - 2].phase, app::Phase::Retrying);
    EXPECT_FALSE(updates.back().lastError.empty());
    EXPECT_EQ(updates.back().phase, app::Phase::Stopped);
}

} // namespace
