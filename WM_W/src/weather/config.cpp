#include "weather/config.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
    bool isCitySlug(std::string_view slug) {
        return !slug.empty() && std::ranges::all_of(slug, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
    }

    double coordinate(const boost::json::object &city, const char *field, double limit) {
        const auto *value = city.if_contains(field);
        if (value && value->is_number()) {
            const auto number = value->to_number<double>();
            if (number >= -limit && number <= limit)
                return number;
        }
        throw std::runtime_error{std::string{"Invalid "} + field + " in the city configuration."};
    }

    std::int64_t stationId(std::string_view key) {
        if (key.empty() || key.front() < '1' || key.front() > '9')
            throw std::runtime_error{"Station IDs must be positive integers without leading zeros."};

        std::int64_t id{};
        const auto end = key.data() + key.size();
        const auto [last, error] = std::from_chars(key.data(), end, id);
        if (error != std::errc{} || last != end)
            throw std::runtime_error{"Station IDs must be positive integers within the supported range."};
        return id;
    }
}

namespace weather {
    CityConfig loadCities(const std::filesystem::path &path) {
        std::ifstream input{path, std::ios::binary};
        if (!input)
            throw std::runtime_error{"Could not open the city configuration at " + path.string() + "."};

        boost::system::error_code error;
        const auto document = boost::json::parse(input, error);
        if (error || !document.is_object())
            throw std::runtime_error{"The city configuration must be a valid JSON object."};

        const auto &root = document.as_object();
        const auto *cities = root.if_contains("cities");
        if (!cities || !cities->is_object() || cities->as_object().empty())
            throw std::runtime_error{"The city configuration must define at least one city in the cities object."};

        CityConfig config;
        for (const auto &city : cities->as_object()) {
            const std::string_view slug{city.key().data(), city.key().size()};
            if (!isCitySlug(slug) || !city.value().is_object())
                throw std::runtime_error{"Each city needs a lowercase slug and an object with its coordinates."};
            const auto &coordinates = city.value().as_object();
            config.cities.emplace(slug, CityCoordinates{coordinate(coordinates, "latitude", 90),
                                                        coordinate(coordinates, "longitude", 180)});
        }

        if (const auto *stations = root.if_contains("stations")) {
            if (!stations->is_object())
                throw std::runtime_error{"The stations field must be an object mapping station IDs to cities."};
            for (const auto &station : stations->as_object()) {
                const auto id = stationId({station.key().data(), station.key().size()});
                if (!station.value().is_string())
                    throw std::runtime_error{"Each station must be assigned a city slug."};
                const auto &slug = station.value().as_string();
                std::string city{slug.data(), slug.size()};
                if (!config.cities.contains(city))
                    throw std::runtime_error{"Station " + std::to_string(id) + " references an unknown city."};
                config.stations.emplace(id, std::move(city));
            }
        }
        return config;
    }
}
