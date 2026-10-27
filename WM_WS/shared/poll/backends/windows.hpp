#pragma once

#include "poll/result.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <expected>
#include <system_error>

namespace poll::internal {

struct WindowsBackend {
    static std::expected<Result, std::error_code> poll(std::chrono::milliseconds timeout) {
        const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
        if (input == nullptr)
            return Result{Status::Closed};
        if (input == INVALID_HANDLE_VALUE) {
            const DWORD error = ::GetLastError();
            return failure(error == ERROR_SUCCESS ? ERROR_INVALID_HANDLE : error);
        }
        DWORD mode{};
        const bool console = ::GetConsoleMode(input, &mode) != 0;
        const DWORD type = ::GetFileType(input);
        if (!console && type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK)
            return std::unexpected(std::make_error_code(std::errc::operation_not_supported));

        // A zero-timeout call still examines the console records already queued.
        DWORD pendingEvents{};
        if (console && timeout.count() == 0 &&
            !::GetNumberOfConsoleInputEvents(input, &pendingEvents))
            return failure(::GetLastError());
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() < 0)
                remaining = std::chrono::milliseconds{0};
            if (console) {
                const auto ready =
                    ::WaitForSingleObject(input, static_cast<DWORD>(remaining.count()));
                if (ready == WAIT_TIMEOUT)
                    return Result{};
                if (ready == WAIT_FAILED)
                    return failure(::GetLastError());
                INPUT_RECORD event{};
                DWORD count{};
                if (!::ReadConsoleInputA(input, &event, 1, &count))
                    return failure(::GetLastError());
                // Console handles also signal for key-up, mouse, and resize events.
                if (count && event.EventType == KEY_EVENT && event.Event.KeyEvent.bKeyDown) {
                    const char character = event.Event.KeyEvent.uChar.AsciiChar;
                    if (character == '\x1a')
                        return Result{Status::Closed}; // Ctrl-Z.
                    if (character != '\0')
                        return Result{Status::Input, character};
                }
            } else {
                if (type == FILE_TYPE_PIPE) {
                    DWORD available{};
                    if (!::PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
                        return failure(::GetLastError());
                    if (available == 0) {
                        if (remaining.count() == 0)
                            return Result{};
                        ::Sleep(
                            static_cast<DWORD>(remaining.count() < 10 ? remaining.count() : 10));
                        continue;
                    }
                }
                char character{};
                DWORD count{};
                if (!::ReadFile(input, &character, 1, &count, nullptr)) {
                    const DWORD error = ::GetLastError();
                    if (error != ERROR_MORE_DATA || count != 1)
                        return failure(error);
                }
                return count ? Result{Status::Input, character} : Result{Status::Closed};
            }
            if (console && timeout.count() == 0 && pendingEvents > 1)
                --pendingEvents;
            else if (std::chrono::steady_clock::now() >= deadline)
                return Result{};
        }
    }

  private:
    static std::expected<Result, std::error_code> failure(DWORD error) {
        if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF ||
            error == ERROR_PIPE_NOT_CONNECTED)
            return Result{Status::Closed};
        return std::unexpected(std::error_code{static_cast<int>(error), std::system_category()});
    }
};

} // namespace poll::internal
