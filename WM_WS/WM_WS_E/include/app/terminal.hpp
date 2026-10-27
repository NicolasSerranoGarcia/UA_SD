#pragma once

#include "app/options.hpp"
#include <atomic>
#include <stop_token>

class Engine;
namespace kafka { class Service; }

namespace app {

void readConsole(std::stop_token stop, Engine &engine, kafka::Service &kafka,
                 const Options &options, std::stop_source &shutdown, std::atomic<bool> &failed);

} // namespace app
