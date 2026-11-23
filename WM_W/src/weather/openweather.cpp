#include "weather/openweather.hpp"

#include "http/client.hpp"
#include <boost/json.hpp>
#include <boost/url.hpp>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace weather {
namespace {
std::string decimal(double value) {
    char buffer[32];
    const auto [last, error] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general);
    if (error != std::errc{})
        throw std::runtime_error("Could not format a city coordinate.");
    return {buffer, last};
}

bool includesRain(std::int64_t code) {
    return (code >= 500 && code < 600) || (code >= 200 && code <= 202) ||
           (code >= 310 && code <= 314) || code == 615 || code == 616;
}

std::expected<Observation, std::string> decode(std::string_view body) {
    boost::system::error_code error;
    const auto json = boost::json::parse(body, error);
    if (error || !json.is_object())
        return std::unexpected("Invalid JSON from OpenWeather.");

    const auto &root = json.as_object();
    const auto *main = root.if_contains("main");
    const auto *temp = main && main->is_object() ? main->as_object().if_contains("temp") : nullptr;
    const auto *conditions = root.if_contains("weather");
    const auto *timestamp = root.if_contains("dt");
    if (!temp || !temp->is_number() || !std::isfinite(temp->to_number<double>()) ||
        !conditions || !conditions->is_array() || conditions->as_array().empty() ||
        !timestamp || !timestamp->is_int64() || timestamp->as_int64() <= 0 ||
        timestamp->as_int64() > 253402300799)
        return std::unexpected("OpenWeather returned an incomplete observation.");

    bool raining = false;
    for (const auto &condition : conditions->as_array()) {
        const auto *code = condition.is_object() ? condition.as_object().if_contains("id") : nullptr;
        if (!code || !code->is_int64() || code->as_int64() < 200 || code->as_int64() >= 900)
            return std::unexpected("OpenWeather returned an invalid condition code.");
        raining = raining || includesRain(code->as_int64());
    }
    using namespace std::chrono;
    return Observation{temp->to_number<double>(), raining, sys_seconds{seconds{timestamp->as_int64()}},
                       floor<seconds>(system_clock::now())};
}
}

OpenWeather::OpenWeather(std::string baseUrl, std::string apiKey)
    : endpoint_(wm_http::endpoint(baseUrl, "/data/2.5/weather")), apiKey_(std::move(apiKey)) {}

std::expected<Observation, std::string> OpenWeather::fetch(const CityCoordinates &city) const {
    boost::urls::url url{endpoint_};
    auto query = url.params();
    query.append({"lat", decimal(city.latitude)});
    query.append({"lon", decimal(city.longitude)});
    query.append({"units", "metric"});
    query.append({"appid", apiKey_});

    const auto reply = wm_http::request(std::string{url.buffer()});
    if (!reply)
        return std::unexpected(reply.error());
    if (reply->status != 200)
        return std::unexpected("OpenWeather returned HTTP " + std::to_string(reply->status) + ".");
    return decode(reply->body);
}

} // namespace weather
