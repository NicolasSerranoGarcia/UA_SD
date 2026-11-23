#include "app/options.hpp"
#include "http/client.hpp"
#include "http/server.hpp"
#include "weather/config.hpp"
#include "weather/service.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <utility>

int main(int argc, char *argv[]) {
    try {
        const auto parsed = app::parseOptions(argc, argv, std::cout);
        if (!parsed) {
            std::cerr << "WM_W could not start. " << parsed.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!*parsed)
            return EXIT_SUCCESS;

        const auto &options = **parsed;
        auto cities = weather::loadCities(options.cityFile);
        if (options.checkConfig) {
            std::cout << "WM_W configuration is valid. HTTP port: " << options.port
                      << ". Tracked cities: " << cities.cities.size() << ".\n";
            return EXIT_SUCCESS;
        }

        const wm_http::CurlSession http;
        weather::Service service{options.cityFile, std::move(cities),
                                 weather::OpenWeather{options.openWeatherApiUrl, options.openWeatherApiKey},
                                 weather::CentralClient{options.centralApiUrl}};
        std::cout << "WM_W starting weather service on port " << options.port << "." << std::endl;
        wm_http::serve(options.port, service);
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "WM_W could not start. " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}