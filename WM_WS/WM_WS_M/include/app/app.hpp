#pragma once

#include "app/options.hpp"

namespace app {

// Runs until a fatal startup or server error. Returns a process exit status.
[[nodiscard]] int run(const Options &options);

} // namespace app
