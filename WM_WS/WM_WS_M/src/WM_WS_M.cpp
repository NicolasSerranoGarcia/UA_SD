#include "app/app.hpp"
#include "app/options.hpp"
#include "logging.hpp"

#if BUILD_GUI
#include "gui/run.hpp"
#endif

#include <exception>
#include <iostream>
#include <memory>
#include <ninttp/socket/error/socket_error.hpp>

int main(int argc, char *argv[]) {
    std::unique_ptr<logging::Session> log;
    try {
        const auto parsed = app::parseOptions(argc, argv, std::cout);
        if (!parsed) {
            logging::fatal("Monitor", "Invalid startup configuration.", parsed.error(),
                           "Run WM_WS_M --help and correct the supplied arguments.");
            return 1;
        }
        if (!*parsed)
            return 0;
        log = std::make_unique<logging::Session>((**parsed).logFile);
#if BUILD_GUI
        return gui::run(argc, argv, **parsed);
#else
        return app::runTerminal(**parsed);
#endif
    } catch (const ninttp::SocketError &error) {
        logging::fatal("Monitor", "A socket operation failed and Monitor cannot continue.", error.what(),
                       "Check Engine/Central connectivity and restart Monitor.");
    } catch (const std::exception &error) {
        logging::fatal("Monitor", "Monitor could not continue.", error.what(),
                       "Check the log file path and configuration, then restart Monitor.");
    }
    return 1;
}
