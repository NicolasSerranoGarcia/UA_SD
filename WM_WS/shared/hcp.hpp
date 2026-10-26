#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <span>
#include <expected>
#include <optional>
#include <chrono>
#include <thread>
#include <ninttp/platform_traits.hpp>
#include <ninttp/socket/error/socket_error.hpp>
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

enum class IoFailure { Socket, Closed, Timeout };
struct IoError {
    IoFailure kind;
    std::optional<ninttp::SocketError> cause = std::nullopt;
    std::string message() const {
        if (kind == IoFailure::Timeout) return "HCP operation timed out";
        if (kind == IoFailure::Closed) return "HCP connection closed";
        return cause ? cause->what() : "HCP socket error";
    }
};
using Deadline = std::chrono::steady_clock::time_point;

// A deadline requires a nonblocking socket. Progress never resets the deadline.
// Only consume requested bytes, retaining subsequent messages in the socket.
template<class Buffer, class Operation>
std::expected<void, IoError> transferExact(Buffer buffer, Operation operation,
                                         std::optional<Deadline> deadline) {
    std::optional<ninttp::SocketError> lastError;
    while (!buffer.empty()) {
        if (deadline && std::chrono::steady_clock::now() >= *deadline)
            return std::unexpected(IoError{IoFailure::Timeout, lastError});
        auto result = operation(buffer);
        if (!result) {
            const auto category = result.error().category();
            if (category == ninttp::SocketErrorCategory::Interrupted) continue;
            if (deadline && category == ninttp::SocketErrorCategory::Blocks) {
                lastError = result.error();
                std::this_thread::sleep_until(std::min(*deadline,
                    std::chrono::steady_clock::now() + std::chrono::milliseconds{10}));
                continue;
            }
            return std::unexpected(IoError{IoFailure::Socket, result.error()});
        }
        if (*result == 0) return std::unexpected(IoError{IoFailure::Closed});
        buffer = buffer.subspan(*result);
    }
    return {};
}

// Retry the public nonblocking connect operation until completion or deadline.
// Each retry uses the same socket so an in-progress connection can finish.
template<class Socket, class Endpoint>
std::expected<void, IoError> connectWithin(Socket& socket, const Endpoint& endpoint,
                                        Deadline deadline) {
    std::optional<ninttp::SocketError> lastError;
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = socket.connect(endpoint);
        if (result) return {};
        // A pending nonblocking connection may finish between connect calls.
#if NINTTP_PLATFORM_WINDOWS == 1
        const bool alreadyConnected = result.error().errorCode() == WSAEISCONN;
#else
        const bool alreadyConnected = result.error().errorCode() == EISCONN;
#endif
        if (lastError && alreadyConnected) return {};
        const auto category = result.error().category();
        if (category != ninttp::SocketErrorCategory::Blocks &&
            category != ninttp::SocketErrorCategory::Interrupted)
            return std::unexpected(IoError{IoFailure::Socket, result.error()});
        lastError = result.error();
        std::this_thread::sleep_until(std::min(deadline,
            std::chrono::steady_clock::now() + std::chrono::milliseconds{10}));
    }
    return std::unexpected(IoError{IoFailure::Timeout, lastError});
}

template<class Socket>
std::expected<void, IoError> receiveExact(Socket& socket, std::span<char> buffer,
                                         std::optional<Deadline> deadline = std::nullopt) {
    return transferExact(buffer, [&](auto part) { return socket.receive(part); }, deadline);
}

template<class Socket>
std::expected<void, IoError> sendExact(Socket& socket, std::string_view message, Deadline deadline) {
    return transferExact(std::span<const char>{message.data(), message.size()},
                         [&](auto part) { return socket.send(part); }, deadline);
}

template<class Socket>
bool receiveId(Socket& socket, std::string& id,
               std::optional<Deadline> deadline = std::nullopt) {
    std::string frame;
    for (std::size_t i = 0; i < 68; ++i) {
        char byte;
        if (!receiveExact(socket, std::span<char>{&byte, 1}, deadline)) return false;
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
