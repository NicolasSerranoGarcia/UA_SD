#include "weather/service.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <syncstream>
#include <utility>
#include <vector>

namespace weather {
namespace {
using namespace std::chrono_literals;
constexpr auto POLL_INTERVAL = 4s;
constexpr auto RETRY_INTERVAL = 2s;
constexpr std::size_t MAX_PARALLEL_REQUESTS = 4;
}

Service::Service(std::filesystem::path path, CityConfig configuration, OpenWeather provider, CentralClient central)
    : path_(std::move(path)), provider_(std::move(provider)), central_(std::move(central)),
      configuration_(std::move(configuration)) {}

Service::~Service() {
    poller_.request_stop();
    publisher_.request_stop();
}

void Service::start() {
    poller_ = std::jthread([this](std::stop_token stop) { poll(stop); });
    publisher_ = std::jthread([this](std::stop_token stop) { publish(stop); });
}

Snapshot Service::snapshot(std::string_view city) const {
    std::scoped_lock lock(mutex_);
    const auto found = samples_.find(city);
    if (found == samples_.end())
        return {configuration_.cities.contains(std::string{city}), std::nullopt, true};
    const auto &sample = found->second;
    return {true, sample.observation, sample.failed || Clock::now() - sample.received > 2 * POLL_INTERVAL};
}

void Service::reload() {
    try {
        auto next = loadCities(path_);
        std::scoped_lock lock(mutex_);
        for (const auto &[station, city] : configuration_.stations) {
            if (!next.stations.contains(station))
                throw std::runtime_error("Keep existing station IDs when updating their cities.");
        }

        for (auto &[station, delivery] : deliveries_) {
            const auto &previousCity = configuration_.stations.at(station);
            const auto &nextCity = next.stations.at(station);
            if (previousCity != nextCity || configuration_.cities.at(previousCity) != next.cities.at(nextCity))
                delivery.desired.reset();
        }

        std::erase_if(samples_, [&](const auto &entry) {
            const auto city = next.cities.find(entry.first);
            return city == next.cities.end() || city->second != configuration_.cities.at(entry.first);
        });
        if (configuration_.cities != next.cities || configuration_.stations != next.stations)
            std::osyncstream(std::cerr) << "[Weather] Updated city configuration." << std::endl;
        configuration_ = std::move(next);
        reloadError_.clear();
    } catch (const std::exception &error) {
        if (reloadError_ != error.what()) {
            reloadError_ = error.what();
            std::osyncstream(std::cerr) << "[Weather] Keeping current cities. " << reloadError_ << std::endl;
        }
    }
}

void Service::poll(std::stop_token stop) {
    auto tick = Clock::now();
    while (!stop.stop_requested()) {
        reload();
        std::vector<std::pair<std::string, CityCoordinates>> cities;
        {
            std::scoped_lock lock(mutex_);
            cities.assign(configuration_.cities.begin(), configuration_.cities.end());
        }

        // Join this cycle's workers before loading a new configuration.
        {
            std::atomic_size_t next{0};
            std::vector<std::jthread> workers;
            for (std::size_t i = 0; i < std::min(cities.size(), MAX_PARALLEL_REQUESTS); ++i) {
                workers.emplace_back([&] {
                    while (!stop.stop_requested()) {
                        const auto index = next.fetch_add(1, std::memory_order_relaxed);
                        if (index >= cities.size())
                            break;
                        const auto &[city, coordinates] = cities[index];
                        update(city, coordinates);
                    }
                });
            }
        }

        do {
            tick += POLL_INTERVAL;
        } while (tick <= Clock::now());
        std::unique_lock lock(mutex_);
        wake_.wait_until(lock, stop, tick, [] { return false; });
    }
}

void Service::update(const std::string &city, const CityCoordinates &coordinates) {
    const auto observed = provider_.fetch(coordinates);
    std::scoped_lock lock(mutex_);
    if (!observed) {
        if (auto sample = samples_.find(city); sample != samples_.end())
            sample->second.failed = true;
        std::osyncstream(std::cerr) << "[Weather] " << city << ": " << observed.error() << std::endl;
        return;
    }

    const auto previous = samples_.find(city);
    if (previous == samples_.end() || previous->second.observation.restricted() != observed->restricted())
        std::osyncstream(std::cerr) << "[Weather] " << city << ": "
                                   << (observed->restricted() ? "irrigation restricted." : "conditions suitable.")
                                   << std::endl;
    samples_.insert_or_assign(city, Sample{*observed, Clock::now(), false});
    for (const auto &[station, assignedCity] : configuration_.stations) {
        if (assignedCity != city)
            continue;
        auto &delivery = deliveries_[station];
        Restriction desired{city, observed->restricted()};
        if (delivery.desired != desired) {
            delivery.desired = std::move(desired);
            delivery.retryAt = Clock::time_point::min();
        }
    }
    wake_.notify_all();
}

void Service::publish(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    while (!stop.stop_requested()) {
        const auto findReady = [&] {
            const auto now = Clock::now();
            auto ready = deliveries_.end();
            for (auto item = deliveries_.begin(); item != deliveries_.end(); ++item) {
                const auto &delivery = item->second;
                if (delivery.desired && delivery.desired != delivery.acknowledged && now >= delivery.retryAt &&
                    (ready == deliveries_.end() || delivery.retryAt < ready->second.retryAt))
                    ready = item;
            }
            return ready;
        };

        wake_.wait_for(lock, stop, RETRY_INTERVAL, [&] { return findReady() != deliveries_.end(); });
        if (stop.stop_requested())
            break;
        const auto ready = findReady();
        if (ready == deliveries_.end())
            continue;

        const auto station = ready->first;
        const auto sent = *ready->second.desired;
        lock.unlock();
        const auto result = central_.setRestriction(station, sent);
        lock.lock();

        auto &delivery = deliveries_.at(station);
        if (result) {
            delivery.acknowledged = sent;
            std::osyncstream(std::cerr) << "[Central] WS-" << station << " (" << sent.city << "): "
                                       << (sent.active ? "restriction acknowledged." : "clearance acknowledged.")
                                       << std::endl;
        } else {
            // A missing reply can mean Central applied the write. Reconcile with the newest state.
            delivery.acknowledged.reset();
            if (delivery.desired == sent)
                delivery.retryAt = Clock::now() + RETRY_INTERVAL;
            std::osyncstream(std::cerr) << "[Central] WS-" << station << ": " << result.error()
                                       << " Will retry." << std::endl;
        }
    }
}

} // namespace weather
