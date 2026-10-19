#include <iostream>
#include "../../shared/hcp.hpp"
#include <cstdint>
#include <string>
#include <ninttp/endpoints.hpp>
#include <string_view>
#include <memory>
#include <functional>

#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>

#include <librdkafka/rdkafkacpp.h>

#include <boost/program_options.hpp>

#include "engine.hpp"


constexpr const int HCP_PORT = 8080;

namespace po = boost::program_options;

using namespace ninttp;

bool monitorHealth(const Engine& engine, StreamSocket<IPv4Endpoint>& socket,
                   std::chrono::milliseconds timeout) {
    std::array<char, 2> request;
    if (!hcp::receiveExact(socket, request, std::chrono::steady_clock::now() + timeout) ||
        std::string_view(request.data(), request.size()) != "HC") return false;
    return hcp::sendExact(socket, engine.isHealthy() ? hcp::okMsg : hcp::koMsg,
                          std::chrono::steady_clock::now() + timeout).has_value();
}

int main(int argc, char* argv[]) {

    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("queue", po::value<std::string>(), "Set the Kafka broker endpoint address (usually hostname for TCP proxy)")
    ("hcp-port", po::value<int>()->default_value(HCP_PORT), "Local HCP listening port (1-65535)")
    ("hcp-timeout-ms", po::value<int>()->default_value(2000), "HCP receive/send deadline in milliseconds");

    po::variables_map vm;
    //stores in the variables map 'vm' as a table, the parsed options from the command line options
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& error) {
        std::cerr << "Error: " << error.what() << "\n" << desc << "\n";
        return 1;
    }

    if(vm.count("help")){
        std::cout << desc << "\n";
        return 0;
    }

    const std::string_view queueEndpoint = vm.count("queue") ? std::string_view{vm["queue"].as<std::string>()} : "";

    if(queueEndpoint.empty()){
        std::cerr << "Error: missing queue endpoint argument\n" << desc << "\n";
        return 1;
    }

    const int hcpPort = vm["hcp-port"].as<int>();
    if (hcpPort < 1 || hcpPort > 65535) {
        std::cerr << "Error: hcp-port must be between 1 and 65535\n";
        return 1;
    }

    const int timeoutMs = vm["hcp-timeout-ms"].as<int>();
    if (timeoutMs <= 0) {
        std::cerr << "Error: HCP timeout must be positive\n";
        return 1;
    }
    const auto timeout = std::chrono::milliseconds{timeoutMs};

    Engine engine;

    auto healthStatusWorker =
    [](Engine& engine, const int hcpPort, std::chrono::milliseconds timeout){
        const auto deadline = [&] { return std::chrono::steady_clock::now() + timeout; };

        ListenerSocket<IPv4Endpoint, StreamSocket<IPv4Endpoint>> listener(Protocol::Tcp, true);

        auto b = listener.bind(IPv4Endpoint{0, static_cast<std::uint16_t>(hcpPort)});

        if (!b) {
            std::cerr << b.error().what() << std::endl;
            return false;
        }
        if (auto result = listener.listen(1); !result) {
            std::cerr << result.error().what() << std::endl;
            return false;
        }

        while (true) {
            auto acceptRes = listener.accept(false);
            if (!acceptRes) {
                std::cerr << acceptRes.error().what() << std::endl;
                return false;
            }

            std::string wsId;
            if (!hcp::receiveId(acceptRes.value(), wsId, deadline())) {
                (void)hcp::sendExact(acceptRes.value(), hcp::koMsg, deadline());
                std::cerr << "Error: missing, timed out, or invalid HCP WS ID\n";
                continue;
            }
            engine.setWsId(wsId);
            if (auto result = hcp::sendExact(acceptRes.value(), hcp::okMsg, deadline()); !result) {
                std::cerr << result.error().message() << std::endl;
                continue;
            }
            std::cout << "HCP station ID: " << engine.wsId() << std::endl;
            while (monitorHealth(engine, acceptRes.value(), timeout)) {}
            std::cerr << "HCP session ended; waiting for Monitor to reconnect\n";
        }

    };

    auto ioWorker = [](Engine& engine){
        while(true){
            char key;
            std::cin >> key;
            if(std::tolower(key) == 'f'){
                engine.injectFault();
            }

            if(std::tolower(key) == 'r'){
                engine.repair();
            }
        }
    };

    auto createProducer = [](std::string_view queueEndpoint) -> std::unique_ptr<RdKafka::Producer> {
        std::unique_ptr<RdKafka::Conf> config{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};

        std::string err;
        if (config->set("bootstrap.servers", queueEndpoint.data(), err) != RdKafka::Conf::CONF_OK) {
            std::cerr << err << '\n';
            return nullptr;
        }

        std::unique_ptr<RdKafka::Producer> producer{RdKafka::Producer::create(config.get(), err)};

        if (!producer) {
            std::cerr << err << '\n';
            return nullptr;
        }
        return producer;
    };

    //returns a valid producer or not. We do not need to retry constructing a valid producer in the thread
    auto producer = createProducer(queueEndpoint);
    if (!producer) return 1;

    bool healthSucceeded = false;
    std::thread healthThread{[&] {
        healthSucceeded = healthStatusWorker(engine, hcpPort, timeout);
    }};

    std::thread inputThread{ioWorker, std::ref(engine)};

    inputThread.join();
    healthThread.join();
    return healthSucceeded ? 0 : 1;
}
