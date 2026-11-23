#include "app/options.hpp"
#include "weather/config.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

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
        const auto cities = weather::loadCities(options.cityFile);
        std::cout << "WM_W configuration is valid. HTTP port: " << options.port
                  << ". Tracked cities: " << cities.cities.size() << ".\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "WM_W could not start. " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
