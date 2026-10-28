#pragma once

#include "poll/poll.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <string_view>
#include <syncstream>

namespace console {

enum class Kind { Waiting, Ready, Closed, Invalid };
struct Command {
    Kind kind = Kind::Waiting;
    std::string text;
};

inline std::ostream *inputEcho() {
#if NINTTP_PLATFORM_WINDOWS == 1
    DWORD inputMode{}, outputMode{};
    // ReadConsoleInput reads key events without the console's usual line echo.
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &inputMode) &&
        GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &outputMode))
        return &std::cout;
#endif
    return nullptr;
}

// Read complete commands without blocking cooperative shutdown or mixing readers.
class Input {
  public:
    explicit Input(std::ostream *echo = nullptr) : echo_(echo) {}

    Command feed(char character) {
        if (character == '\r' || character == '\n') {
            echo("\n");
            if (overflow_) {
                overflow_ = false;
                line_.clear();
                return {Kind::Invalid, "Command too long; enter one menu option."};
            }
            const auto first = line_.find_first_not_of(" \t");
            if (first == line_.npos) {
                line_.clear();
                return {};
            }
            auto text = line_.substr(first, line_.find_last_not_of(" \t") - first + 1);
            line_.clear();
            for (auto &letter : text)
                letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
            return {Kind::Ready, std::move(text)};
        }
        if (overflow_)
            return {};
        if (character == '\b' || character == '\x7f') {
            if (!line_.empty()) {
                line_.pop_back();
                echo("\b \b");
            }
        } else if (line_.size() >= 64) {
            overflow_ = true;
        } else if (static_cast<unsigned char>(character) >= ' ') {
            line_ += character;
            echo(std::string_view{&character, 1});
        }
        return {};
    }

    std::expected<Command, std::error_code> read(std::chrono::milliseconds timeout) {
        const auto input = poll::poll(timeout);
        if (!input)
            return std::unexpected(input.error());
        if (input->status == poll::Status::Closed)
            return Command{Kind::Closed, {}};
        if (input->status == poll::Status::Timeout)
            return Command{};
        return feed(input->character);
    }

  private:
    void echo(std::string_view text) {
        if (echo_)
            std::osyncstream(*echo_) << text << std::flush;
    }

    std::ostream *echo_;
    std::string line_;
    bool overflow_ = false;
};

} // namespace console
