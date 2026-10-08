#include <iostream>
#include <thread>
#include <chrono>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <boost/program_options.hpp>
#include <ninttp/endpoints.hpp>
#include <ninttp/socket/socket.hpp>

using namespace ninttp;
using namespace std::literals;
namespace po = boost::program_options;
constexpr int HCP_PORT = 8080;

int main(int argc, char* argv[]) {
    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "produce help message")
    ("engine-ip", po::value<std::string>(), "Engine IPv4 address in dotted-decimal format (required)")
    ("engine-port", po::value<int>()->default_value(HCP_PORT), "Engine HCP port (1-65535)");

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

    const std::string_view engineIp = vm.count("engine-ip")
        ? std::string_view{vm["engine-ip"].as<std::string>()} : "";

    if (engineIp.empty()) {
        std::cerr << "Error: missing engine-ip argument\n" << desc << "\n";
        return 1;
    }

    const int enginePort = vm["engine-port"].as<int>();
    if (enginePort < 1 || enginePort > 65535) {
        std::cerr << "Error: engine-port must be between 1 and 65535\n";
        return 1;
    }

    const auto engineEndpoint = IPv4Endpoint::fromString(
        engineIp, static_cast<std::uint16_t>(enginePort));
    if (!engineEndpoint) {
        std::cerr << "Error: engine-ip must be a valid dotted-decimal IPv4 address\n";
        return 1;
    }

    StreamSocket<IPv4Endpoint> hcpStreamer(Protocol::Tcp);
    if (auto result = hcpStreamer.connect(engineEndpoint.value()); !result) {
        std::cerr << result.error().what() << std::endl;
        return 1;
    }

    std::array<char, 512> buf;
    while (true) {
        std::this_thread::sleep_for(1s);

        if (auto result = hcpStreamer.sendAll(std::string_view{"HC"}); !result) {
            std::cerr << result.error().what() << std::endl;
            return 1;
        }

        auto received = hcpStreamer.receive(buf);
        if (!received) {
            std::cerr << received.error().what() << std::endl;
            return 1;
        }
        if (*received == 0) break;

        std::cout.write(buf.data(), *received);
        std::cout << std::endl;
    }
    return 0;
}
