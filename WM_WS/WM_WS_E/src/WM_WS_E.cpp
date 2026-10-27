#include "app/app.hpp"
#include "app/options.hpp"
#include "logging.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <ninttp/socket/error/socket_error.hpp>

int main(int argc, char *argv[]) {
    std::unique_ptr<logging::Session> log;
    try {
        const auto parsed = app::parseOptions(argc, argv, std::cout);
        if (!parsed) {
            logging::fatal("Engine", "Invalid startup configuration.", parsed.error(),
                           "Run WM_WS_E --help and correct the supplied arguments.");
            return 1;
        }
        if (!*parsed)
            return 0;
        log = std::make_unique<logging::Session>((**parsed).logFile);
        return app::run(**parsed);
    } catch (const ninttp::SocketError &error) {
        logging::fatal("Engine", "A socket operation failed and Engine cannot continue.", error.what(),
                       "Check the HCP address/port and restart Engine.");
    } catch (const std::exception &error) {
        logging::fatal("Engine", "Engine could not continue.", error.what(),
                       "Check the log/state file paths and permissions, then restart Engine.");
    }
    return 1;
}
