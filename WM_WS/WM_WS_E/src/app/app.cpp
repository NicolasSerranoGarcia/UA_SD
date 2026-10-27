#include "app/app.hpp"

#include "app/health_server.hpp"
#include "app/terminal.hpp"
#include "engine/engine.hpp"
#include "kafka/service.hpp"
#include "logging.hpp"
#include <atomic>
#include <thread>

namespace app {

int run(const Options &options) {
    logging::record() << "[Engine][Kafka] waiting for Monitor activation; bootstrap="
                      << options.queueEndpoint << std::endl;
    Engine engine;
    kafka::Service kafka(engine, options.queueEndpoint, options.stateFile);
    std::stop_source shutdown;
    std::atomic<bool> consoleFailed = false;
    std::jthread input([&](std::stop_token stop) {
        readConsole(stop, engine, kafka, options, shutdown, consoleFailed);
    });
    const auto result = serveHealth(engine, options.hcpPort, options.hcpTimeout, shutdown.get_token());
    input.request_stop();
    input.join();
    if (!result) {
        logging::fatal("Engine", "Engine cannot accept health checks from Monitor.", result.error(),
                       "Another program may be using Engine's connection port. Choose a free port with --hcp-port and use that port in Monitor too.");
        return 1;
    }
    return consoleFailed.load() ? 1 : 0;
}

} // namespace app
