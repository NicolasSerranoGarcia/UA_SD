#include "app/health_server.hpp"

#include "engine/engine.hpp"
#include "hcp.hpp"
#include "logging.hpp"
#include <array>
#include <iostream>
#include <ninttp/endpoints.hpp>
#include <ninttp/socket/socket.hpp>
#include <string_view>
#include <syncstream>
#include <thread>

namespace app {
namespace {

hcp::Deadline deadline(std::chrono::milliseconds timeout) {
    return std::chrono::steady_clock::now() + timeout;
}

std::expected<void, std::string> respondToHealth(Engine &engine,
                                                 ninttp::StreamSocket<ninttp::IPv4Endpoint> &socket,
                                                 std::chrono::milliseconds timeout) {
    std::array<char, 2> request{};
    if (const auto result = hcp::receiveExact(socket, request, deadline(timeout)); !result)
        return std::unexpected(result.error().message());
    const std::string_view command{request.data(), request.size()};
    if (command != "HC" && command != "AC")
        return std::unexpected(std::string{"invalid HCP health request"});
    const bool accepted = command == "AC" ? engine.activateFromMonitor().has_value() : engine.checkHealth();
    const auto status = accepted ? hcp::okMsg : hcp::koMsg;
    if (const auto result = hcp::sendExact(socket, status, deadline(timeout));
        !result)
        return std::unexpected(result.error().message());
    logging::record() << "[Monitor][HCP] ws=" << engine.wsId()
                               << (command == "AC" ? " activation response=" : " health response=")
                               << status << std::endl;
    return {};
}

} // namespace

std::expected<void, std::string> serveHealth(Engine &engine, std::uint16_t port,
                                             std::chrono::milliseconds timeout, std::stop_token stop) {
    ninttp::ListenerSocket<ninttp::IPv4Endpoint, ninttp::StreamSocket<ninttp::IPv4Endpoint>>
        listener(ninttp::Protocol::Tcp, false);
    if (const auto result = listener.bind(ninttp::IPv4Endpoint{0, port}); !result)
        return std::unexpected(std::string{"HCP bind: "} + result.error().what());
    if (const auto result = listener.listen(1); !result)
        return std::unexpected(std::string{"HCP listen: "} + result.error().what());
    logging::record() << "[Monitor][HCP] listening on 0.0.0.0:" << port << std::endl;

    while (!stop.stop_requested()) {
        auto accepted = listener.accept(false);
        if (!accepted) {
            if (accepted.error().category() == ninttp::SocketErrorCategory::Blocks) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continue;
            }
            if (accepted.error().category() == ninttp::SocketErrorCategory::Interrupted)
                continue;
            return std::unexpected(std::string{"HCP accept: "} + accepted.error().what());
        }
        auto &socket = *accepted;
        // Every exit path revokes activation, including malformed requests and timeouts.
        struct MonitorSession {
            Engine &engine;
            ~MonitorSession() { engine.setMonitorConnected(false); }
        } session{engine};
        std::string wsId;
        if (!hcp::receiveId(socket, wsId, deadline(timeout))) {
            (void)hcp::sendExact(socket, hcp::koMsg, deadline(timeout));
            logging::record()
                << "[Monitor][HCP] identification rejected: missing, timed out, or invalid WS ID\n";
            continue;
        }
        if (!engine.wsId().empty() && engine.wsId() != wsId) {
            (void)hcp::sendExact(socket, hcp::koMsg, deadline(timeout));
            logging::record() << "[Monitor][HCP] identification rejected: station ID changed\n";
            continue;
        }
        engine.setWsId(wsId);
        engine.setMonitorConnected(true);
        if (const auto result = hcp::sendExact(socket, hcp::okMsg, deadline(timeout)); !result) {
            logging::record() << "[Monitor][HCP] ws=" << wsId
                                       << " identification response failed: "
                                       << result.error().message() << '\n';
            continue;
        }
        logging::record() << "[Monitor][HCP] ws=" << wsId << " identification accepted"
                                   << std::endl;
        while (!stop.stop_requested()) {
            if (const auto result = respondToHealth(engine, socket, timeout); !result) {
                logging::record() << "[Monitor][HCP] ws=" << wsId
                                           << " session ended: " << result.error()
                                           << "; waiting for Monitor to reconnect\n";
                break;
            }
        }
    }
    return {};
}

} // namespace app
