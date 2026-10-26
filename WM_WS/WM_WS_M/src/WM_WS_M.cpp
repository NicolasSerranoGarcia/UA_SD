#include "app/app.hpp"
#include "app/options.hpp"

#if BUILD_GUI
#include "gui/run.hpp"
#endif

#include <exception>
#include <iostream>
#include <ninttp/socket/error/socket_error.hpp>

int main(int argc, char *argv[]) {
    try {
        const auto parsed = app::parseOptions(argc, argv, std::cout);
        if (!parsed) {
            std::cerr << "Error: " << parsed.error() << '\n';
            return 1;
        }
        if (!*parsed)
            return 0;
#if BUILD_GUI
        return gui::run(argc, argv, **parsed);
#else
        return app::run(**parsed);
#endif
    } catch (const ninttp::SocketError &error) {
        std::cerr << "Error: " << error.what() << '\n';
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
    }
    return 1;
}
