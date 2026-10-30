#pragma once

#include <boost/json.hpp>
#include <string>
#include <string_view>

namespace kafka {

// UUID-sized operation codes stay exact in checkpoints and numeric on the wire.
[[nodiscard]] boost::json::object parseCommand(std::string_view payload);
[[nodiscard]] std::string operationKey(const boost::json::value &operation);
[[nodiscard]] std::string serializeEvent(const boost::json::object &value);

} // namespace kafka
