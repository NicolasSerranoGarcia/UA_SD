#include "app/health_server.hpp"

#include "engine/engine.hpp"
#include "hcp.hpp"
#include <array>
#include <iostream>
#include <ninttp/endpoints.hpp>
#include <ninttp/socket/socket.hpp>
#include <string_view>
#include <syncstream>

namespace app {
namespace {

hcp::Deadline deadline(std::chrono::milliseconds timeout) {
    return std::chrono::steady_clock::now() + timeout;
}

std::expected<void, std::string> respondToHealth(const Engine &engine,
                                                 ninttp::StreamSocket<ninttp::IPv4Endpoint> &socket,
                                                 std::chrono::milliseconds timeout) {
    std::array<char, 2> request{};
    if (const auto result = hcp::receiveExact(socket, request, deadline(timeout)); !result)
        return std::unexpected(result.error().message());
    if (std::string_view{request.data(), request.size()} != "HC")
        return std::unexpected(std::string{"invalid HCP health request"});
    const auto status = engine.isHealthy() ? hcp::okMsg : hcp::koMsg;
    if (const auto result = hcp::sendExact(socket, status, deadline(timeout));
        !result)
        return std::unexpected(result.error().message());
    std::osyncstream(std::cout) << "[Monitor][HCP] ws=" << engine.wsId()
                               << " health response=" << status << std::endl;
    return {};
}

} // namespace

std::expected<void, std::string> serveHealth(Engine &engine, std::uint16_t port,
                                             std::chrono::milliseconds timeout) {
    ninttp::ListenerSocket<ninttp::IPv4Endpoint, ninttp::StreamSocket<ninttp::IPv4Endpoint>>
        listener(ninttp::Protocol::Tcp, true);
    if (const auto result = listener.bind(ninttp::IPv4Endpoint{0, port}); !result)
        return std::unexpected(std::string{"HCP bind: "} + result.error().what());
    if (const auto result = listener.listen(1); !result)
        return std::unexpected(std::string{"HCP listen: "} + result.error().what());
    std::osyncstream(std::cout) << "[Monitor][HCP] listening on 0.0.0.0:" << port << std::endl;

    while (true) {
        auto accepted = listener.accept(false);
        if (!accepted) {
            if (accepted.error().category() == ninttp::SocketErrorCategory::Interrupted)
                continue;
            return std::unexpected(std::string{"HCP accept: "} + accepted.error().what());
        }
        auto &socket = *accepted;
        std::string wsId;
        if (!hcp::receiveId(socket, wsId, deadline(timeout))) {
            (void)hcp::sendExact(socket, hcp::koMsg, deadline(timeout));
            std::osyncstream(std::cerr)
                << "[Monitor][HCP] identification rejected: missing, timed out, or invalid WS ID\n";
            continue;
        }
        engine.setWsId(wsId);
        if (const auto result = hcp::sendExact(socket, hcp::okMsg, deadline(timeout)); !result) {
            std::osyncstream(std::cerr) << "[Monitor][HCP] ws=" << wsId
                                       << " identification response failed: "
                                       << result.error().message() << '\n';
            continue;
        }
        std::osyncstream(std::cout) << "[Monitor][HCP] ws=" << wsId << " identification accepted"
                                   << std::endl;
        while (true) {
            if (const auto result = respondToHealth(engine, socket, timeout); !result) {
                std::osyncstream(std::cerr) << "[Monitor][HCP] ws=" << wsId
                                           << " session ended: " << result.error()
                                           << "; waiting for Monitor to reconnect\n";
                break;
            }
        }
    }
}

} // namespace app
