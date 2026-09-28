#include <iostream>
#include <ninttp/dns/dns.hpp>
#include <librdkafka/rdkafkacpp.h>
#include <ninttp/socket/socket.hpp>

using namespace ninttp;

int main() {
    const char* hostname = "google.com";
    auto addresses = dns::resolve(hostname);
    if (!addresses) {
        std::cerr << "DNS lookup failed for " << hostname
                  << ": resolver error " << addresses.error() << '\n';
        return 1;
    }

    std::cout << "DNS results for " << hostname << ":\n";
    for (const auto& endpoint : addresses->ipv4) {
        const auto address = endpoint.addressHostOrder();
        std::cout << "  IPv4: " << ((address >> 24) & 0xff) << '.'
                  << ((address >> 16) & 0xff) << '.'
                  << ((address >> 8) & 0xff) << '.' << (address & 0xff) << '\n';
    }
    for (const auto& endpoint : addresses->ipv6) {
        const auto bytes = endpoint.addressBytes();
        std::cout << "  IPv6: " << std::hex;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            if (i != 0) std::cout << ':';
            std::cout << ((static_cast<unsigned>(bytes[i]) << 8) | bytes[i + 1]);
        }
        std::cout << std::dec << '\n';
    }


    std::cout << "Kafka client: " << RdKafka::version() << "\n";

    bool blocks = false;

    int port = 8080;

    ListenerSocket<IPv4Endpoint, StreamSocket<IPv4Endpoint>> listener(Protocol::Tcp, blocks);

    auto b = listener.bind(IPv4Endpoint::loopback(port));

    StreamSocket<IPv4Endpoint> socket(Protocol::Tcp);

    auto c = socket.connect(IPv4Endpoint::loopback(port));

    return 0;
}
