#include <iostream>
#include <cstdint>
#include <string>
#include <ninttp/endpoints.hpp>
#include <string_view>
#include <memory>

#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>

#include <librdkafka/rdkafkacpp.h>

#include <boost/program_options.hpp>

#include "engine.hpp"

#include "hcp_helper/hcp_helper.hpp"

constexpr const int HCP_PORT = 8080;

namespace po = boost::program_options;

using namespace ninttp;

bool monitorHealth(const Engine& engine, StreamSocket<IPv4Endpoint>& hcpStreamer){
    static std::array<char, 512> buf;

    auto got = hcpStreamer.receive(buf);

    if(!got && got.error().category() != ninttp::SocketErrorCategory::Blocks){
        std::cout << got.error().what() << std::endl;
    }

    if(!got && got.error().category() == ninttp::SocketErrorCategory::Blocks){
        return false;
    }

    //this is all wrong. We may receive less than 2 bytes per reception. Still, I find it complicated to pipeline
    //the data. 
    if(got.value() < 2){
        return false;
    }

    if(std::string{std::span{buf}.subspan(0, 2).data()} == std::string{"HC"}){
        if(engine.isHealthy()){
            hcpStreamer.sendAll({HCPHelper::okMsg});
        } else{
            hcpStreamer.sendAll({HCPHelper::koMsg});
        }
    }

    return true;
}

int main(int argc, char* argv[]) {

    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("queue", po::value<std::string>(), "Set the Kafka broker endpoint address (usually hostname for TCP proxy)")
    ("hcp-port", po::value<int>()->default_value(HCP_PORT), "Local HCP listening port (1-65535)");

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

    Engine engine;
    engine.start();

    ListenerSocket<IPv4Endpoint, StreamSocket<IPv4Endpoint>> listener(Protocol::Tcp, true);

    auto b = listener.bind(IPv4Endpoint{0, static_cast<std::uint16_t>(hcpPort)});

    if (!b) {
        std::cerr << b.error().what() << std::endl;
        return 1;
    }
    if (auto result = listener.listen(1); !result) {
        std::cerr << result.error().what() << std::endl;
        return 1;
    }

    auto acceptRes = listener.accept();
    if (!acceptRes) {
        std::cerr << acceptRes.error().what() << std::endl;
        return 1;
    }

    acceptRes.value().setNonblocking(true);

    std::unique_ptr<RdKafka::Conf> config{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};

    std::string err;
    if (config->set("bootstrap.servers", queueEndpoint.data(), err) != RdKafka::Conf::CONF_OK) {
        std::cerr << err << '\n';
        return 1;
    }

    std::unique_ptr<RdKafka::Producer> producer{RdKafka::Producer::create(config.get(), err)};

    if (!producer) {
        std::cerr << err << '\n';
        return 1;
    }

    while(true){
        //producer->produce("station-status", 1 );
        monitorHealth(engine, acceptRes.value());
    }
}
