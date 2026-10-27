#pragma once

#include "poll/result.hpp"
#include <ninttp/platform_traits.hpp>

#if NINTTP_PLATFORM_WINDOWS == 1
#include "poll/backends/windows.hpp"
#elif NINTTP_PLATFORM_UNIX == 1
#include "poll/backends/posix.hpp"
#else
#error "poll: unsupported platform"
#endif

#include <chrono>
#include <expected>
#include <limits>
#include <system_error>

namespace poll {

#if NINTTP_PLATFORM_WINDOWS == 1
using SelectedBackend = internal::WindowsBackend;
#elif NINTTP_PLATFORM_UNIX == 1
using SelectedBackend = internal::PosixBackend;
#endif

// Poll stdin for one character. This must be stdin's only reader; do not mix with cin.
// Timeout is a finite duration in [0, INT_MAX] milliseconds; zero checks immediately.
[[nodiscard]] inline std::expected<Result, std::error_code>
poll(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > std::numeric_limits<int>::max())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    return SelectedBackend::poll(timeout);
}

} // namespace poll
