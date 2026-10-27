#pragma once

namespace poll {

enum class Status { Timeout, Input, Closed };

struct Result {
    Status status = Status::Timeout;
    char character = '\0'; // Valid only for Status::Input.
};

} // namespace poll
