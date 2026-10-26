#pragma once

#include "app/options.hpp"
#include "app/status.hpp"

#include <stop_token>

namespace app {

// Runs until a fatal startup error or stop request. Returns a process exit status.
[[nodiscard]] int run(const Options &options, std::stop_token stop = {},
                      StatusObserver observer = {});

} // namespace app
