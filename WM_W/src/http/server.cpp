#include "http/server.hpp"

#include "weather/service.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <csignal>
#include <exception>
#include <format>
#include <string_view>
#include <utility>

namespace wm_http {
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using asio::ip::tcp;
using namespace std::chrono_literals;

using Reply = http::response<http::string_body>;

Reply reply(http::status status, unsigned version, boost::json::object body) {
    Reply response{status, version};
    response.set(http::field::content_type, "application/json");
    response.set(http::field::cache_control, "no-store");
    response.keep_alive(false);
    response.body() = boost::json::serialize(body);
    response.prepare_payload();
    return response;
}

Reply route(const http::request<http::string_body> &request, const weather::Service &service) {
    const auto version = request.version();
    if (request.method() != http::verb::get) {
        auto response = reply(http::status::method_not_allowed, version, {{"error", "Use GET."}});
        response.set(http::field::allow, "GET");
        return response;
    }

    const auto raw = request.target();
    const std::string_view path{raw.data(), raw.size()};
    if (path == "/health")
        return reply(http::status::ok, version, {{"status", "ok"}});

    constexpr std::string_view prefix = "/weather/";
    if (!path.starts_with(prefix))
        return reply(http::status::not_found, version, {{"error", "Unknown route."}});
    const auto city = path.substr(prefix.size());
    const auto snapshot = service.snapshot(city);
    if (!snapshot.tracked)
        return reply(http::status::not_found, version, {{"error", "City is not tracked."}});
    if (!snapshot.observation)
        return reply(http::status::service_unavailable, version, {{"error", "Waiting for a weather reading."}});

    const auto &observation = *snapshot.observation;
    return reply(http::status::ok, version, {
        {"city", boost::json::string(city)}, {"temperature_c", observation.temperatureC},
        {"raining", observation.raining}, {"restricted", observation.restricted()},
        {"observed_at", std::format("{:%FT%TZ}", observation.observedAt)},
        {"fetched_at", std::format("{:%FT%TZ}", observation.fetchedAt)}, {"stale", snapshot.stale}});
}

asio::awaitable<void> session(tcp::socket socket, const weather::Service &service) {
    beast::tcp_stream stream{std::move(socket)};
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.header_limit(8192);
    parser.body_limit(0);
    stream.expires_after(5s);
    co_await http::async_read(stream, buffer, parser, asio::use_awaitable);

    auto response = route(parser.get(), service);
    stream.expires_after(5s);
    co_await http::async_write(stream, response, asio::use_awaitable);
    beast::error_code error;
    stream.socket().shutdown(tcp::socket::shutdown_send, error);
}

asio::awaitable<void> accept(tcp::acceptor &listener, const weather::Service &service) {
    for (;;) {
        auto socket = co_await listener.async_accept(asio::use_awaitable);
        asio::co_spawn(listener.get_executor(), session(std::move(socket), service), asio::detached);
    }
}
}

void serve(std::uint16_t port, weather::Service &service) {
    asio::io_context context{1};
    tcp::acceptor listener{context, tcp::endpoint{tcp::v4(), port}};
    asio::signal_set signals{context, SIGINT, SIGTERM};
    signals.async_wait([&](const boost::system::error_code &error, int) {
        if (!error)
            context.stop();
    });
    asio::co_spawn(context, accept(listener, service), [](const std::exception_ptr &error) {
        if (error)
            std::rethrow_exception(error);
    });
    service.start();
    context.run();
}

} // namespace wm_http
