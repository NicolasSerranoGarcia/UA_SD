#pragma once

#include <cstdint>

namespace weather { class Service; }

namespace wm_http {

void serve(std::uint16_t port, weather::Service &service);

} // namespace wm_http
