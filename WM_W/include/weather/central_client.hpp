#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace weather {

struct Restriction {
    std::string city;
    bool active;
    bool operator==(const Restriction &) const = default;
};

class CentralClient {
public:
    explicit CentralClient(std::string baseUrl);
    std::expected<void, std::string> setRestriction(std::int64_t stationId, const Restriction &restriction) const;

private:
    std::string baseUrl_;
};

} // namespace weather
