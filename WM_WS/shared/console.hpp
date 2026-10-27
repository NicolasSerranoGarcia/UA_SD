#pragma once

#include "poll/poll.hpp"

#include <cctype>
#include <string>

namespace console {

enum class Kind { Waiting, Ready, Closed, Invalid };
struct Command {
    Kind kind = Kind::Waiting;
    std::string text;
};

// Read complete commands without blocking cooperative shutdown or mixing readers.
class Input {
  public:
    Command feed(char character) {
        if (character == '\r' || character == '\n') {
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
            if (!line_.empty())
                line_.pop_back();
        } else if (line_.size() >= 64) {
            overflow_ = true;
        } else if (static_cast<unsigned char>(character) >= ' ') {
            line_ += character;
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
    std::string line_;
    bool overflow_ = false;
};

} // namespace console
