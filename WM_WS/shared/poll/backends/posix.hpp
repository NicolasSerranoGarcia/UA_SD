#pragma once

#include "poll/result.hpp"

#include <cerrno>
#include <chrono>
#include <expected>
#include <sys/select.h>
#include <system_error>
#include <unistd.h>

namespace poll::internal {

struct PosixBackend {
    static std::expected<Result, std::error_code> poll(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            auto remaining = std::chrono::ceil<std::chrono::microseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() < 0)
                remaining = std::chrono::microseconds{0};
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining);
            timeval wait{static_cast<time_t>(seconds.count()),
                         static_cast<suseconds_t>((remaining - seconds).count())};
            fd_set input;
            FD_ZERO(&input);
            FD_SET(STDIN_FILENO, &input);
            // select avoids the global ::poll name, which would conflict with namespace poll.
            const int ready = ::select(STDIN_FILENO + 1, &input, nullptr, nullptr, &wait);
            if (ready == 0)
                return Result{};
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                return std::unexpected(std::error_code{errno, std::generic_category()});
            }
            char character{};
            const auto count = ::read(STDIN_FILENO, &character, 1);
            if (count > 0)
                return Result{Status::Input, character};
            if (count == 0)
                return Result{Status::Closed};
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                return std::unexpected(std::error_code{errno, std::generic_category()});
        } while (std::chrono::steady_clock::now() < deadline);
        return Result{};
    }
};

} // namespace poll::internal
