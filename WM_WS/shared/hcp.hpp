#pragma once

#include <span>
#include <string>
#include <string_view>

namespace hcp {
inline constexpr std::string_view okMsg = "OK";
inline constexpr std::string_view koMsg = "KO";

inline bool validId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

// Request only the remaining bytes so subsequent messages stay in the socket.
template<class Socket>
bool receiveExact(Socket& socket, std::span<char> buffer) {
    while (!buffer.empty()) {
        auto received = socket.receive(buffer);
        if (!received || *received == 0) return false;
        buffer = buffer.subspan(*received);
    }
    return true;
}

template<class Socket>
bool receiveId(Socket& socket, std::string& id) {
    std::string frame;
    for (std::size_t i = 0; i < 68; ++i) {
        char byte;
        if (!receiveExact(socket, std::span<char>{&byte, 1})) return false;
        if (byte == '\n') {
            if (!frame.starts_with("ID ") || !validId(std::string_view{frame}.substr(3))) return false;
            id = frame.substr(3);
            return true;
        }
        frame += byte;
    }
    return false;
}
}
