#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <stop_token>

class Engine;

namespace app {

// Serves successive HCP sessions. A broken session does not stop the listener.
[[nodiscard]] std::expected<void, std::string> serveHealth(Engine &engine, std::uint16_t port,
                                                           std::chrono::milliseconds timeout,
                                                           std::stop_token stop = {});

} // namespace app
