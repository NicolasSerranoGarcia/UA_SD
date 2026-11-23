#pragma once

#include "weather/central_client.hpp"
#include "weather/openweather.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

namespace weather {

struct Snapshot {
    bool tracked;
    std::optional<Observation> observation;
    bool stale;
};

class Service {
public:
    Service(std::filesystem::path path, CityConfig configuration, OpenWeather provider, CentralClient central);
    ~Service();

    void start();
    Snapshot snapshot(std::string_view city) const;

private:
    using Clock = std::chrono::steady_clock;

    struct Sample {
        Observation observation;
        Clock::time_point received;
        bool failed;
    };

    struct Delivery {
        std::optional<Restriction> desired;
        std::optional<Restriction> acknowledged;
        Clock::time_point retryAt = Clock::time_point::min();
    };

    void poll(std::stop_token stop);
    void reload();
    void update(const std::string &city, const CityCoordinates &coordinates);
    void publish(std::stop_token stop);

    const std::filesystem::path path_;
    const OpenWeather provider_;
    const CentralClient central_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    CityConfig configuration_;
    std::map<std::string, Sample, std::less<>> samples_;
    std::map<std::int64_t, Delivery> deliveries_;
    std::string reloadError_;
    std::jthread poller_;
    std::jthread publisher_;
};

} // namespace weather
