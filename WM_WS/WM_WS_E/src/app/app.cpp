#include "app/app.hpp"

#include "app/health_server.hpp"
#include "engine/engine.hpp"
#include "kafka/service.hpp"
#include "poll/poll.hpp"
#include <chrono>
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <stop_token>
#include <string>
#include <system_error>
#include <syncstream>
#include <thread>

namespace app {
namespace {

// Poll before reading so a fatal server error can stop this worker even with stdin open.
// EOF disables console commands; the service continues in containers without a terminal.
void readConsole(std::stop_token stop, Engine &engine, kafka::Service &kafka) {
    try {
        while (!stop.stop_requested()) {
            const auto input = poll::poll(std::chrono::milliseconds{100});
            if (!input)
                throw std::system_error(input.error(), "polling console input");
            if (input->status == poll::Status::Closed) {
                std::osyncstream(std::cout)
                    << "[Engine][Console] input closed; health service continues" << std::endl;
                return;
            }
            if (input->status == poll::Status::Timeout)
                continue;
            const char key = input->character;
            switch (key) {
            case 's':
            case 'S':
                std::osyncstream(std::cout) << "[Engine][Console] local irrigation "
                    << (kafka.startLocal() ? "started" : "denied: station is not activated or available")
                    << std::endl;
                break;
            case 'e':
            case 'E':
                kafka.stopLocal();
                break;
            case 'f':
            case 'F':
                engine.injectFault();
                std::osyncstream(std::cout) << "[Engine][Console] fault injected; health=KO"
                                           << std::endl;
                break;
            case 'r':
            case 'R':
                engine.repair();
                std::osyncstream(std::cout) << "[Engine][Console] repair completed; health=OK"
                                           << std::endl;
                break;
            case 'q':
            case 'Q':
                std::osyncstream(std::cout) << "[Engine][Console] 'Q' pressed; exiting..."
                    << std::endl;
                std::exit(EXIT_SUCCESS);
            default:
                break;
            }
        }
    } catch (const std::exception &error) {
        std::osyncstream(std::cerr) << "[Engine][Console] input failed: " << error.what() << '\n';
    }
}

} // namespace

int run(const Options &options) {
    std::osyncstream(std::cout) << "[Engine][Kafka] waiting for Monitor activation; bootstrap="
                               << options.queueEndpoint << std::endl;
    Engine engine;
    kafka::Service kafka(engine, options.queueEndpoint, options.stateFile);
    std::jthread input([&engine, &kafka](std::stop_token stop) { readConsole(stop, engine, kafka); });
    const auto result = serveHealth(engine, options.hcpPort, options.hcpTimeout);
    if (!result) {
        std::osyncstream(std::cerr) << "[Monitor][HCP] health service failed: " << result.error()
                                   << '\n';
        return 1;
    }
    return 0;
}

} // namespace app
