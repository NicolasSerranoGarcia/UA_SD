#include "app/app.hpp"

#include "app/health_server.hpp"
#include "engine/engine.hpp"
#include "poll/poll.hpp"
#include <chrono>
#include <exception>
#include <expected>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <memory>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

namespace app {
namespace {

std::expected<std::unique_ptr<RdKafka::Producer>, std::string>
createProducer(const std::string &endpoint) {
    std::unique_ptr<RdKafka::Conf> config{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
    if (!config)
        return std::unexpected(std::string{"creating Kafka configuration failed"});
    std::string error;
    if (config->set("bootstrap.servers", endpoint, error) != RdKafka::Conf::CONF_OK)
        return std::unexpected(error);
    std::unique_ptr<RdKafka::Producer> producer{RdKafka::Producer::create(config.get(), error)};
    if (!producer)
        return std::unexpected(error);
    return producer;
}

// Poll before reading so a fatal server error can stop this worker even with stdin open.
// EOF disables console commands; the service continues in containers without a terminal.
void readConsole(std::stop_token stop, Engine &engine) {
    try {
        while (!stop.stop_requested()) {
            const auto input = poll::poll(std::chrono::milliseconds{100});
            if (!input)
                throw std::system_error(input.error(), "polling console input");
            if (input->status == poll::Status::Closed)
                return;
            if (input->status == poll::Status::Timeout)
                continue;
            const char key = input->character;
            switch (key) {
            case 'f':
            case 'F':
                engine.injectFault();
                break;
            case 'r':
            case 'R':
                engine.repair();
                break;
            default:
                break;
            }
        }
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
    }
}

} // namespace

int run(const Options &options) {
    auto producer = createProducer(options.queueEndpoint);
    if (!producer) {
        std::cerr << "Error: " << producer.error() << '\n';
        return 1;
    }
    Engine engine;
    std::jthread input([&engine](std::stop_token stop) { readConsole(stop, engine); });
    const auto result = serveHealth(engine, options.hcpPort, options.hcpTimeout);
    if (!result) {
        std::cerr << "Error: " << result.error() << '\n';
        return 1;
    }
    return 0;
}

} // namespace app
