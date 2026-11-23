#include "weather/central_client.hpp"

#include "http/client.hpp"
#include <boost/json.hpp>
#include <utility>

namespace weather {

CentralClient::CentralClient(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {}

std::expected<void, std::string> CentralClient::setRestriction(std::int64_t stationId,
                                                               const Restriction &restriction) const {
    const auto url = wm_http::endpoint(baseUrl_, "/api/stations/" + std::to_string(stationId) + "/weather-restriction");
    const auto body = boost::json::serialize(boost::json::object{
        {"city", restriction.city}, {"active", restriction.active}});
    const auto reply = wm_http::request(url, body);
    if (!reply)
        return std::unexpected(reply.error());
    if (reply->status < 200 || reply->status >= 300)
        return std::unexpected("Central returned HTTP " + std::to_string(reply->status) + ".");
    return {};
}

} // namespace weather
