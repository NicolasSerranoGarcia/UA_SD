#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace weather {
    struct CityCoordinates {
        double latitude;
        double longitude;
    };

    struct CityConfig {
        std::map<std::string, CityCoordinates> cities;
        std::map<std::int64_t, std::string> stations;
    };

    CityConfig loadCities(const std::filesystem::path &path);
}
