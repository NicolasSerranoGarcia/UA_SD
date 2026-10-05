#include <iostream>
#include <cstdint>
#include <string>
#include <ninttp/endpoints.hpp>
#include <string_view>
#include <coroutine>

#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>

#include <librdkafka/rdkafkacpp.h>

#include <boost/program_options.hpp>

#include "engine.hpp"

#include "hcp_helper/hcp_helper.hpp"

constexpr const int HCP_PORT = 8080;

namespace po = boost::program_options;

using namespace ninttp;

bool monitorHealth(const Engine& engine, StreamSocket<IPv4Endpoint>& hcpStreamer, std::chrono::seconds statusSendTimeout = 1s){
    if(statusSendTimeout < 0s){
        return false;
    }

    static auto nextHealthStatusSend = std::chrono::steady_clock::now() + statusSendTimeout;

    if(std::chrono::steady_clock::now() < nextHealthStatusSend){
        return false;
    }
    nextHealthStatusSend += statusSendTimeout;

    //send 

    if(engine.isHealthy()){
        hcpStreamer.sendAll(HCPHelper::okMsg);
    } else{
        hcpStreamer.sendAll(HCPHelper::koMsg);
    }

}

int main(int argc, char* argv[]) {

    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("queue", po::value<std::string>(), "Set the Kafka broker endpoint address (usually hostname for TCP proxy)")
    ("monitor-ip", po::value<std::string>(), "Monitor IPv4 address in dotted-decimal format (required)")
    ("monitor-port", po::value<int>()->default_value(HCP_PORT), "Monitor HCP port (1-65535)");

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

    const std::string_view monitorIp = vm.count("monitor-ip")
        ? std::string_view{vm["monitor-ip"].as<std::string>()} : "";

    if (monitorIp.empty()) {
        std::cerr << "Error: missing monitor-ip argument\n" << desc << "\n";
        return 1;
    }

    const int monitorPort = vm["monitor-port"].as<int>();
    if (monitorPort < 1 || monitorPort > 65535) {
        std::cerr << "Error: monitor-port must be between 1 and 65535\n";
        return 1;
    }

    const auto monitorEndpoint = IPv4Endpoint::fromString(
        monitorIp, static_cast<std::uint16_t>(monitorPort));
    if (!monitorEndpoint) {
        std::cerr << "Error: monitor-ip must be a valid dotted-decimal IPv4 address\n";
        return 1;
    }

    std::cout << "monitor: " << monitorIp << ':' << monitorEndpoint->portHostOrder()
              << ", queue: " << queueEndpoint << std::endl;

    Engine engine;

    StreamSocket<IPv4Endpoint> hcpStreamer(Protocol::Tcp);

    if(auto connectRes = hcpStreamer.connect(monitorEndpoint.value()); !connectRes){
        std::cerr << connectRes.error().what() << std::endl;
        return 1;
    }

    while(true){
        auto engineSdTime = engine.timeToShutdown();
        if(engineSdTime < 0s){
            engine.stop();

            //probably notify central via kafka
        } else{
            std::cout << "Remaining time to shutdown: " << engineSdTime << std::endl;
        }

        monitorHealth(engine, hcpStreamer);
    }

}
