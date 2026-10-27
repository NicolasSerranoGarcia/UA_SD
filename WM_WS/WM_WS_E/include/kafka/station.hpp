#pragma once

#include "engine/engine.hpp"

#include <boost/json.hpp>
#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace kafka {

[[nodiscard]] std::expected<int, std::string> partitionFor(std::string_view recipient, int count);

struct Event {
    std::string topic;
    std::string recipient;
    boost::json::object value;
};

// Implements the README's station messages; transport serializes access.
class Station {
  public:
    Station(Engine &engine, std::string id);
    using BeforeAction = std::function<void()>;
    [[nodiscard]] std::expected<void, std::string> handle(
        std::string_view payload, BeforeAction beforeAction = {});
    [[nodiscard]] bool startLocal(BeforeAction beforeAction = {});
    void stopLocal();
    void tick();
    std::deque<Event> &events() { return events_; }
    [[nodiscard]] boost::json::object checkpoint() const;
    [[nodiscard]] std::expected<void, std::string> restore(const boost::json::object &state);

  private:
    void reply(std::uint64_t operation, bool success, std::string_view reason = {});
    void finish();

    Engine &engine_;
    std::string id_;
    std::uint64_t numericId_;
    std::deque<Event> events_;
    std::map<std::string, boost::json::object> replies_;
    std::map<std::string, boost::json::object> commands_;
    std::optional<boost::json::object> session_;
    std::optional<boost::json::object> intent_;
    std::chrono::steady_clock::time_point nextTelemetry_{};
    bool blocked_ = false;
};

} // namespace kafka
