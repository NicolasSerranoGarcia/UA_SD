#pragma once

#include "weather/config.hpp"
#include "weather/observation.hpp"

#include <expected>
#include <string>

namespace weather {

class OpenWeather {
public:
    OpenWeather(std::string baseUrl, std::string apiKey);
    std::expected<Observation, std::string> fetch(const CityCoordinates &city) const;

private:
    std::string endpoint_;
    std::string apiKey_;
};

} // namespace weather
