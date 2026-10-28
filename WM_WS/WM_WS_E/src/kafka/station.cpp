#include "kafka/station.hpp"

#include <charconv>
#include <stdexcept>

namespace kafka {
namespace {

std::uint64_t integer(const boost::json::value &value) {
    if ((!value.is_int64() && !value.is_uint64()) || (value.is_int64() && value.as_int64() < 0))
        throw std::invalid_argument("ID/code must be a nonnegative JSON integer");
    return value.to_number<std::uint64_t>();
}

std::string numericId(std::string_view value) {
    std::uint64_t id = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), id);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
        throw std::invalid_argument("WS Kafka ID must be a decimal integer");
    return std::to_string(id);
}

} // namespace

std::expected<int, std::string> partitionFor(std::string_view recipient, int count) {
    if (count <= 0 || recipient.empty())
        return std::unexpected("partition count must be positive and recipient ID nonempty");
    std::int64_t partition = 0;
    for (unsigned char digit : recipient) {
        if (digit < '0' || digit > '9')
            return std::unexpected("Kafka routing requires a decimal numeric ID");
        partition = (partition * 10 + digit - '0') % count;
    }
    return static_cast<int>(partition);
}

Station::Station(Engine &engine, std::string id)
    : engine_(engine), id_(numericId(id)), numericId_(std::stoull(id_)) {}

void Station::reply(std::uint64_t operation, bool success, std::string_view reason) {
    boost::json::object value{{"station_id", numericId_}, {"operation_code", operation}, {"status", success ? "OK" : "KO"},
        {"reason", success ? boost::json::value{} : boost::json::value(reason)}};
    replies_[std::to_string(operation)] = value;
    events_.push_back({"ws-start-conf", id_, std::move(value)});
}

std::expected<void, std::string> Station::handle(std::string_view payload, BeforeAction beforeAction) {
    std::optional<std::uint64_t> addressedOperation;
    try {
        const auto received = boost::json::parse(payload).as_object();
        if (integer(received.at("station_id")) != numericId_)
            return {};
        const auto operation = integer(received.at("operation_code"));
        addressedOperation = operation;
        const auto type = std::string(received.at("type").as_string());
        if (type != "IRRIGATION" && type != "ACTIVATE" && type != "BLOCK")
            throw std::invalid_argument("unknown Central operation");
        const auto owner = received.at("operator_id");
        if (!owner.is_null())
            (void)integer(owner);
        std::optional<std::chrono::seconds> duration;
        if (type == "IRRIGATION") {
            if (const auto *limit = received.if_contains("duration"); limit && !limit->is_null()) {
                const auto seconds = integer(*limit);
                const auto maximum = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::time_point::max() - std::chrono::steady_clock::now()).count();
                if (seconds == 0 || seconds >= static_cast<std::uint64_t>(maximum))
                    throw std::invalid_argument("duration must be positive seconds within the supported clock range");
                duration = std::chrono::seconds{seconds};
            }
        }
        const boost::json::object command{{"station_id", numericId_}, {"type", type},
            {"operation_code", operation}, {"operator_id", owner},
            {"duration", duration ? boost::json::value(duration->count()) : boost::json::value{}}};
        const auto code = std::to_string(operation);
        if (const auto previous = commands_.find(code); previous != commands_.end()) {
            if (previous->second != command)
                throw std::invalid_argument("operation_code was reused for a different command");
            events_.push_back({"ws-start-conf", id_, replies_.at(code)});
            return {};
        }
        commands_[code] = command;
        if (!engine_.canPublish()) {
            reply(operation, false, "Monitor disconnected or startup activation incomplete");
            return {};
        }
        tick();
        // Persist intent before valve changes. A crash must not replay a start as new work.
        intent_ = command;
        replies_[code] = {{"station_id", numericId_}, {"operation_code", operation},
                         {"status", "KO"}, {"reason", "Operation interrupted before acknowledgement"}};
        const bool previousBlock = blocked_;
        if (type == "BLOCK")
            blocked_ = true;
        if (type == "IRRIGATION" && engine_.isActivated() && engine_.isCentralAvailable() && !engine_.isRunning())
            session_ = command;
        if (beforeAction)
            beforeAction();
        if (type == "BLOCK") {
            (void)engine_.block(0);
            blocked_ = true;
            finish();
            reply(operation, true);
        } else if (type == "ACTIVATE") {
            const auto result = engine_.unblock(0);
            if (result)
                blocked_ = false;
            else
                blocked_ = previousBlock;
            reply(operation, result.has_value(), "Station is not healthy or Monitor is unavailable");
        } else {
            const auto result = engine_.start(40, duration);
            if (result) {
                session_ = command;
                nextTelemetry_ = {};
            } else if (!engine_.isRunning()) {
                session_.reset();
            }
            reply(operation, result.has_value(), engine_.isCentralAvailable() ?
                "Station is not activated, healthy, or idle" : "Central is unavailable; wait for recovery before starting");
        }
        intent_.reset();
        return {};
    } catch (const std::exception &error) {
        if (intent_)
            throw; // Persistence failures must stop the service before any further actuation.
        if (addressedOperation) {
            events_.push_back({"ws-start-conf", id_, {{"station_id", numericId_},
                {"operation_code", *addressedOperation}, {"status", "KO"}, {"reason", error.what()}}});
        }
        return std::unexpected(std::string{"invalid Kafka command: "} + error.what());
    }
}

bool Station::startLocal(BeforeAction beforeAction) {
    if (!engine_.canPublish() || !engine_.isActivated() || !engine_.isCentralAvailable() ||
        !engine_.isHealthy() || engine_.isRunning())
        return false;
    tick();
    intent_ = boost::json::object{{"station_id", numericId_}, {"operation_code", nullptr},
        {"type", "IRRIGATION"}, {"duration", nullptr}, {"operator_id", nullptr}};
    session_ = *intent_;
    if (beforeAction)
        beforeAction();
    if (!engine_.start(40, std::nullopt)) {
        intent_.reset();
        session_.reset();
        return false;
    }
    events_.push_back({"ws-start-conf", id_, {{"station_id", numericId_},
        {"operation_code", nullptr}, {"status", "OK"}, {"reason", nullptr}}});
    intent_.reset();
    nextTelemetry_ = {};
    return true;
}

void Station::finish() {
    if (!session_)
        return;
    const auto reading = engine_.sampleMeter().reading;
    boost::json::object value{{"station_id", numericId_}, {"status", "FINISHED"},
        {"total_liters", reading.volumeLiters}, {"total_time", reading.elapsedSeconds},
        {"operator_id", session_->at("operator_id")}};
    events_.push_back({"ws-finish-conf", id_, std::move(value)});
    session_.reset();
}

void Station::stopLocal() {
    (void)engine_.stop();
    finish();
}

void Station::tick() {
    if (!session_)
        return;
    if (!engine_.isRunning()) {
        finish();
        return;
    }
    if (!engine_.canPublish() || std::chrono::steady_clock::now() < nextTelemetry_)
        return;
    const auto owner = session_->at("operator_id");
    boost::json::object value{{"station_id", numericId_},
        {"flux_l_min", engine_.reading().flowLitersPerMinute}, {"operator_id", owner}};
    const auto recipient = owner.is_null() ? id_ : std::to_string(integer(owner));
    // Never replace the front event: it may already be in flight to the broker.
    auto pending = events_.empty() ? events_.end() : std::next(events_.begin());
    for (; pending != events_.end(); ++pending) {
        if (pending->topic == "telemetry" && pending->recipient == recipient)
            break;
    }
    if (pending != events_.end())
        *pending = Event{"telemetry", recipient, std::move(value)};
    else
        events_.push_back({"telemetry", recipient, std::move(value)});
    nextTelemetry_ = std::chrono::steady_clock::now() + std::chrono::seconds{1};
}

boost::json::object Station::checkpoint(std::optional<Engine::Reading> sample) const {
    boost::json::array pending;
    for (const auto &event : events_)
        pending.push_back(boost::json::object{{"topic", event.topic}, {"recipient", event.recipient}, {"value", event.value}});
    boost::json::object replies, commands;
    for (const auto &[key, value] : replies_)
        replies[key] = value;
    for (const auto &[key, value] : commands_)
        commands[key] = value;
    auto reading = sample ? *sample : engine_.reading();
    if (intent_ && session_ && !engine_.isRunning() && intent_->at("type") == "IRRIGATION" &&
        intent_->at("operation_code") == session_->at("operation_code"))
        reading = {};
    return {{"version", 1}, {"station_id", id_}, {"blocked", blocked_}, {"pending", pending},
            {"replies", replies}, {"commands", commands},
            {"session", session_ ? boost::json::value(*session_) : boost::json::value{}},
            {"intent", intent_ ? boost::json::value(*intent_) : boost::json::value{}},
            {"volume_l", reading.volumeLiters}, {"elapsed_s", reading.elapsedSeconds}};
}

std::expected<void, std::string> Station::restore(const boost::json::object &state) {
    try {
        if (const auto *version = state.if_contains("version"); version && version->to_number<int>() != 1)
            return std::unexpected("unsupported station checkpoint version");
        if (state.at("station_id").as_string() != id_)
            return std::unexpected("journal belongs to another station");
        engine_.restoreMeter({0, state.at("volume_l").to_number<double>(), state.at("elapsed_s").to_number<double>()});
        blocked_ = state.at("blocked").as_bool();
        if (blocked_)
            (void)engine_.block(0);
        for (const auto &entry : state.at("pending").as_array()) {
            const auto &record = entry.as_object();
            events_.push_back({std::string(record.at("topic").as_string()),
                std::string(record.at("recipient").as_string()), record.at("value").as_object()});
        }
        for (const auto &entry : state.at("replies").as_object())
            replies_[std::string(entry.key())] = entry.value().as_object();
        for (const auto &entry : state.at("commands").as_object())
            commands_[std::string(entry.key())] = entry.value().as_object();
        if (const auto *pending = state.if_contains("intent"); pending && !pending->is_null()) {
            const auto &command = pending->as_object();
            if (command.at("operation_code").is_null()) {
                events_.push_back({"ws-start-conf", id_, {{"station_id", numericId_},
                    {"operation_code", nullptr}, {"status", "KO"}, {"reason", "Local start interrupted"}}});
            } else {
                const auto code = std::to_string(integer(command.at("operation_code")));
                events_.push_back({"ws-start-conf", id_, replies_.at(code)});
            }
        }
        if (!state.at("session").is_null()) {
            session_ = state.at("session").as_object();
            finish();
            events_.back().value["total_liters"] = state.at("volume_l");
            events_.back().value["total_time"] = state.at("elapsed_s");
        }
        return {};
    } catch (const std::exception &error) {
        return std::unexpected(std::string{"invalid station journal: "} + error.what());
    }
}

} // namespace kafka
