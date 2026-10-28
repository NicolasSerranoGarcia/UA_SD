#include "nap/reporter.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;
using Tcp = boost::asio::ip::tcp;

class ReporterPeer : public testing::Test {
  protected:
    boost::asio::io_context context;
    Tcp::acceptor listener{context, Tcp::endpoint(Tcp::v4(), 0)};

    ninttp::IPv4Endpoint endpoint() {
        return ninttp::IPv4Endpoint::loopback(listener.local_endpoint().port());
    }

    Tcp::socket accept() {
        listener.non_blocking(true);
        Tcp::socket socket(context);
        const auto limit = std::chrono::steady_clock::now() + 3s;
        for (;;) {
            boost::system::error_code error;
            listener.accept(socket, error);
            if (!error) {
                socket.non_blocking(true);
                return socket;
            }
            if (error != boost::asio::error::would_block || std::chrono::steady_clock::now() >= limit)
                throw boost::system::system_error(error);
            std::this_thread::sleep_for(1ms);
        }
    }

    std::string receive(Tcp::socket &socket, bool untilClosed = false) {
        std::string frame;
        const auto limit = std::chrono::steady_clock::now() + 3s;
        while (untilClosed || !frame.ends_with("#FIN")) {
            char byte;
            boost::system::error_code error;
            if (socket.read_some(boost::asio::buffer(&byte, 1), error))
                frame += byte;
            else if (error == boost::asio::error::eof)
                return frame;
            else if (error != boost::asio::error::would_block || std::chrono::steady_clock::now() >= limit)
                throw boost::system::system_error(error);
            else
                std::this_thread::sleep_for(1ms);
        }
        return frame;
    }

    void acknowledge(Tcp::socket &socket, unsigned int request = 1, std::string_view id = "station-1") {
        const std::string body = "NAP#1.0#" + std::to_string(request) + "#3#" + std::string(id);
        unsigned char checksum = 0;
        for (unsigned char byte : body)
            checksum ^= byte;
        constexpr char digits[] = "0123456789ABCDEF";
        const std::string frame = body + '#' + digits[checksum >> 4] + digits[checksum & 15] + "#FIN";
        boost::asio::write(socket, boost::asio::buffer(frame));
    }
};

TEST_F(ReporterPeer, ActivatesBeforeHealthyReports) {
    nap::Reporter reporter(endpoint(), "station-1", 100ms, 500ms);
    reporter.update(true, "OK");
    auto activation = accept();
    EXPECT_EQ(receive(activation), "NAP#1.0#1#1#station-1#16#FIN");
    acknowledge(activation);
    activation.close();
    auto status = accept();
    EXPECT_EQ(receive(status), "NAP#1.0#1#2#station-1#OK#32#FIN");
    acknowledge(status);
}

TEST_F(ReporterPeer, HealthyReportsReuseConnectionAndKoClosesIt) {
    nap::Reporter reporter(endpoint(), "station-1", 100ms, 500ms);
    reporter.update(true, "OK");
    auto activation = accept();
    ASSERT_EQ(receive(activation), "NAP#1.0#1#1#station-1#16#FIN");
    acknowledge(activation);
    activation.close();
    auto status = accept();
    ASSERT_EQ(receive(status), "NAP#1.0#1#2#station-1#OK#32#FIN");
    acknowledge(status);
    EXPECT_EQ(receive(status), "NAP#1.0#2#2#station-1#OK#31#FIN");
    acknowledge(status, 2);
    reporter.update(false, "KO");
    EXPECT_EQ(receive(status), "NAP#1.0#3#2#station-1#KO#30#FIN");
    acknowledge(status, 3);
    EXPECT_EQ(receive(status, true), "");
    reporter.update(true, "repaired");
    auto reactivation = accept();
    EXPECT_EQ(receive(reactivation), "NAP#1.0#1#1#station-1#16#FIN");
    acknowledge(reactivation);
}

TEST_F(ReporterPeer, FastRepairCannotSkipUnacknowledgedFault) {
    nap::Reporter reporter(endpoint(), "station-1", 100ms, 500ms);
    reporter.update(false, "KO");
    reporter.update(true, "repaired");
    auto fault = accept();
    ASSERT_EQ(receive(fault), "NAP#1.0#1#2#station-1#KO#32#FIN");
    acknowledge(fault);
    EXPECT_EQ(receive(fault, true), "");
    auto activation = accept();
    EXPECT_EQ(receive(activation), "NAP#1.0#1#1#station-1#16#FIN");
    acknowledge(activation);
}

TEST_F(ReporterPeer, LateActivationCannotOverrideNewerFault) {
    nap::Reporter reporter(endpoint(), "station-1", 20ms, 200ms);
    reporter.update(true, "OK");
    auto activation = accept();
    receive(activation);
    reporter.update(false, "KO");
    acknowledge(activation);
    auto status = accept();
    receive(status);
    acknowledge(status);
    std::this_thread::sleep_for(30ms);
    EXPECT_EQ(reporter.status().activationGeneration, 0);
}

TEST_F(ReporterPeer, UnavailableSnapshotCannotCarryActivation) {
    nap::Reporter reporter(endpoint(), "station-1", 20ms, 200ms);
    reporter.update(true, "OK");
    auto activation = accept();
    receive(activation);
    acknowledge(activation);
    auto status = accept();
    receive(status);
    acknowledge(status);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (reporter.status().activationGeneration == 0) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
    status.close();
    for (;;) {
        const auto snapshot = reporter.status();
        if (!snapshot.available) {
            EXPECT_EQ(snapshot.activationGeneration, 0);
            EXPECT_FALSE(snapshot.lastError.empty());
            break;
        }
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
}

TEST_F(ReporterPeer, InvalidActivationRepliesCannotGrantPermission) {
    for (int failure = 0; failure < 3; ++failure) {
        SCOPED_TRACE(failure);
        nap::Reporter reporter(endpoint(), "station-1", 20ms, 200ms);
        reporter.update(true, "OK");
        auto activation = accept();
        receive(activation);
        if (failure == 0) acknowledge(activation, 2);
        else if (failure == 1) acknowledge(activation, 1, "station-2");
        else {
            constexpr std::string_view response = "NAP#1.0#1#3#station-1#00#FIN";
            boost::asio::write(activation, boost::asio::buffer(response));
        }
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!reporter.status().fatal) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            std::this_thread::yield();
        }
        const auto state = reporter.status();
        EXPECT_FALSE(state.available);
        EXPECT_EQ(state.activationGeneration, 0);
        EXPECT_FALSE(state.lastError.empty());
    }
}

} // namespace
