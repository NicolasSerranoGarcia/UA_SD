#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

class Engine;

namespace app {

// Serves successive HCP sessions. A broken session does not stop the listener.
[[nodiscard]] std::expected<void, std::string> serveHealth(Engine &engine, std::uint16_t port,
                                                           std::chrono::milliseconds timeout);

} // namespace app
