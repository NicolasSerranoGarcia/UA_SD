#include "hcp.hpp"

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <ninttp/socket/socket.hpp>
#include <string_view>

namespace {

TEST(MonitorConnect, CompletesNonblockingConnectionAndTransfersData) {
    boost::asio::io_context context;
    using Tcp = boost::asio::ip::tcp;
    Tcp::acceptor listener(context, Tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    ninttp::StreamSocket<ninttp::IPv4Endpoint> socket(ninttp::Protocol::Tcp, false);
    const auto endpoint = ninttp::IPv4Endpoint::loopback(listener.local_endpoint().port());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

    const auto connected = hcp::connectWithin(socket, endpoint, deadline);
    ASSERT_TRUE(connected.has_value()) << connected.error().message();
    const auto sent = hcp::sendExact(socket, "HC", deadline);
    ASSERT_TRUE(sent.has_value()) << sent.error().message();

    auto peer = listener.accept();
    std::array<char, 2> request{};
    boost::asio::read(peer, boost::asio::buffer(request));
    EXPECT_EQ(std::string_view(request.data(), request.size()), "HC");
}

} // namespace
