#include <iostream>
#include <string_view>

#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>

#include <librdkafka/rdkafkacpp.h>

#include <boost/program_options.hpp>

#include "engine.hpp"

namespace po = boost::program_options;

int main(int argc, char* argv[]) {

    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("queue", po::value<std::string>(), "Set the Kafka broker endpoint address (IP:Port format)")
    ("monitor", po::value<std::string>(), "Set this engine's correspondent monitor endpoint address (IP:Port format)");

    po::variables_map vm;
    //stores in the variables map 'vm' as a table, the parsed options from the command line options
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if(vm.count("help")){
        std::cout << desc << "\n";
        return 0;
    }

    const std::string_view queueEndpoint = vm.count("queue") ? std::string_view{vm["queue"].as<std::string>()} : "";

    if(queueEndpoint.empty()){
        std::cout << "Error: missing queue endpoint argument\n" << desc << "\n";
    }

    const std::string_view monitorEndpoint = vm.count("monitor") ? std::string_view{vm["monitor"].as<std::string>()} : "";

    if(monitorEndpoint.empty()){
        std::cout << "Error: missing monitor endpoint argument\n" << desc << "\n";
    }

    std::cout << "monitor: " << monitorEndpoint << ", queue: " << queueEndpoint << std::endl;

    return 0;
}
