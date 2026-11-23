#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace app { struct Options; }

namespace registry {

enum class Action { Verify, Register, Deregister };

class Enrollment {
public:
    explicit Enrollment(const app::Options &options);
    std::string perform(Action action) const;

private:
    std::string origin_;
    std::string url_;
    std::string city_;
    std::string caFile_;
    std::int64_t stationId_;
    std::filesystem::path path_;
};

} // namespace registry
