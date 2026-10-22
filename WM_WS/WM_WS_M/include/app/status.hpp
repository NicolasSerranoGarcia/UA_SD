#pragma once

#include <chrono>
#include <functional>
#include <ninttp/endpoints.hpp>
#include <optional>
#include <string>

namespace app {

enum class Phase { Resolving, Registering, Monitoring, Retrying, Failed, Stopped };
enum class Health { Unknown, Ok, Ko };

struct Status {
    Phase phase = Phase::Resolving;
    Health health = Health::Unknown;
    std::optional<ninttp::IPv4Endpoint> centralEndpoint;
    std::optional<std::chrono::system_clock::time_point> lastHealth;
    std::string lastError;
};

// Called synchronously on the monitoring thread; adapters own dispatch to their UI.
using StatusObserver = std::function<void(const Status &)>;

} // namespace app
