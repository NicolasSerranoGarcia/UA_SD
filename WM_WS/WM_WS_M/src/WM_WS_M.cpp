#include <iostream>
#include <ninttp/dns/dns.hpp>
#include <ninttp/socket/socket.hpp>

using namespace ninttp;

int main() {
    bool blocks = true;

    int port = 8080;

    ListenerSocket<IPv4Endpoint, StreamSocket<IPv4Endpoint>> listener(Protocol::Tcp, blocks);

    auto b = listener.bind(IPv4Endpoint::loopback(port));

    listener.listen(1);

    auto acceptRes = listener.accept();

    std::array<char, 512> buf;

    while(true){
        acceptRes.value().receive(buf);
        
        std::cout << buf.data() << std::endl;
        
        acceptRes.value().sendAll(std::string_view{"Hello from WM_WS_M"});
    }

    return 0;
}
