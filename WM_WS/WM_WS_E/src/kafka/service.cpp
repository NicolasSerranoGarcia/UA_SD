#include "kafka/service.hpp"

#include "kafka/station.hpp"
#include "kafka/journal.hpp"
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <map>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <syncstream>
#include <thread>

namespace kafka {
namespace {

void configure(RdKafka::Conf &configuration, const std::string &name, const std::string &value) {
    std::string error;
    if (configuration.set(name, value, error) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error(error);
}

} // namespace

struct Service::Impl : RdKafka::DeliveryReportCb {
    Engine &engine;
    std::string endpoint;
    Journal journal;
    std::mutex mutex;
    std::unique_ptr<Station> station;
    std::unique_ptr<RdKafka::Producer> producer;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer;
    std::map<std::string, int> partitions;
    int commandPartition = -1;
    bool inFlight = false;
    bool failed = false;
    std::optional<bool> delivered;
    std::chrono::steady_clock::time_point nextMetadata{};
    std::chrono::steady_clock::time_point nextCheckpoint{};
    std::jthread worker;

    Impl(Engine &engine, std::string endpoint, std::string stateFile)
        : engine(engine), endpoint(std::move(endpoint)), journal(std::move(stateFile)),
          worker([this](std::stop_token stop) { run(stop); }) {}

    ~Impl() {
        worker.request_stop();
        worker.join();
        if (producer)
            (void)producer->purge(RdKafka::Producer::PURGE_QUEUE | RdKafka::Producer::PURGE_INFLIGHT);
        if (consumer)
            (void)consumer->close();
    }

    void dr_cb(RdKafka::Message &message) override {
        delivered = message.err() == RdKafka::ERR_NO_ERROR;
    }

    void save() {
        journal.save(station->checkpoint());
        nextCheckpoint = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    }

    void initialize(const std::string &id) {
        station = std::make_unique<Station>(engine, id);
        const auto latest = journal.load();
        if (latest) {
            if (const auto restored = station->restore(*latest); !restored)
                throw std::runtime_error(restored.error());
        }
        save();
        std::string error;
        std::unique_ptr<RdKafka::Conf> configuration{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        configure(*configuration, "bootstrap.servers", endpoint);
        configure(*configuration, "enable.idempotence", "true");
        configure(*configuration, "message.timeout.ms", "3000");
        configure(*configuration, "socket.timeout.ms", "1000");
        if (configuration->set("dr_cb", this, error) != RdKafka::Conf::CONF_OK)
            throw std::runtime_error(error);
        producer.reset(RdKafka::Producer::create(configuration.get(), error));
        if (!producer)
            throw std::runtime_error(error);
        configuration.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
        configure(*configuration, "bootstrap.servers", endpoint);
        configure(*configuration, "group.id", "wm-ws-" + id);
        configure(*configuration, "enable.auto.commit", "false");
        configure(*configuration, "auto.offset.reset", "latest");
        configure(*configuration, "socket.timeout.ms", "1000");
        consumer.reset(RdKafka::KafkaConsumer::create(configuration.get(), error));
        if (!consumer)
            throw std::runtime_error(error);
    }

    void refreshMetadata(const std::string &id) {
        if (std::chrono::steady_clock::now() < nextMetadata)
            return;
        nextMetadata = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        RdKafka::Metadata *result = nullptr;
        if (producer->metadata(true, nullptr, &result, 100) != RdKafka::ERR_NO_ERROR)
            return;
        const std::unique_ptr<RdKafka::Metadata> metadata{result};
        for (const auto *topic : *metadata->topics()) {
            if (topic->err() == RdKafka::ERR_NO_ERROR)
                partitions[topic->topic()] = static_cast<int>(topic->partitions()->size());
        }
        const auto found = partitions.find("central-ops");
        if (found == partitions.end())
            return;
        const auto desired = partitionFor(id, found->second);
        if (!desired || *desired == commandPartition)
            return;
        std::unique_ptr<RdKafka::TopicPartition> assignment{
            RdKafka::TopicPartition::create("central-ops", *desired, RdKafka::Topic::OFFSET_STORED)};
        if (consumer->assign({assignment.get()}) == RdKafka::ERR_NO_ERROR) {
            commandPartition = *desired;
            std::osyncstream(std::cout) << "[Engine][Kafka] ws=" << id
                                       << " central-ops partition=" << commandPartition << std::endl;
        }
    }

    void publishNext() {
        if (inFlight || station->events().empty() || !engine.canPublish())
            return;
        const auto &event = station->events().front();
        const auto found = partitions.find(event.topic);
        if (found == partitions.end())
            return;
        const auto partition = partitionFor(event.recipient, found->second);
        if (!partition)
            throw std::runtime_error(partition.error());
        const auto value = boost::json::serialize(event.value);
        save();
        const auto error = producer->produce(event.topic, *partition, RdKafka::Producer::RK_MSG_COPY,
            const_cast<char *>(value.data()), value.size(), event.recipient.data(),
            event.recipient.size(), 0, nullptr);
        if (error == RdKafka::ERR_NO_ERROR)
            inFlight = true;
    }

    void run(std::stop_token stop) {
        try {
            while (!stop.stop_requested()) {
                {
                    std::lock_guard lock(mutex);
                    const auto id = engine.wsId();
                    if (!station && !id.empty()) {
                        if (!partitionFor(id, 1)) {
                            // Legacy nonnumeric HCP IDs remain usable for health-only tests.
                            std::osyncstream(std::cerr) << "[Engine][Kafka] numeric WS ID required for modulo routing\n";
                            return;
                        }
                        initialize(id);
                    }
                    if (station) {
                        producer->poll(0);
                        if (delivered) {
                            if (*delivered && !station->events().empty())
                                station->events().pop_front();
                            inFlight = false;
                            delivered.reset();
                            save();
                        }
                        const auto pendingBeforeTick = station->events().size();
                        station->tick();
                        if (pendingBeforeTick != station->events().size() ||
                            (engine.isRunning() && std::chrono::steady_clock::now() >= nextCheckpoint))
                            save();
                        if (!engine.canPublish()) {
                            if (inFlight)
                                (void)producer->purge(RdKafka::Producer::PURGE_QUEUE | RdKafka::Producer::PURGE_INFLIGHT);
                        } else {
                            refreshMetadata(id);
                            if (commandPartition >= 0) {
                                const std::unique_ptr<RdKafka::Message> message{consumer->consume(0)};
                                if (message->err() == RdKafka::ERR_NO_ERROR) {
                                    const auto handled = station->handle(std::string_view{
                                        static_cast<const char *>(message->payload()), message->len()},
                                        [this] { save(); });
                                    if (!handled)
                                        std::osyncstream(std::cerr) << "[Engine][Kafka] " << handled.error() << '\n';
                                    save();
                                    (void)consumer->commitAsync(message.get());
                                }
                            }
                            // Persist pending output before handing it to the broker.
                            if (!station->events().empty() && !inFlight) {
                                publishNext();
                            }
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
        } catch (const std::exception &error) {
            std::lock_guard lock(mutex);
            failed = true;
            if (producer)
                (void)producer->purge(RdKafka::Producer::PURGE_QUEUE | RdKafka::Producer::PURGE_INFLIGHT);
            engine.injectFault();
            std::osyncstream(std::cerr) << "[Engine][Kafka] service stopped safely: " << error.what() << '\n';
        }
    }
};

Service::Service(Engine &engine, std::string endpoint, std::string stateFile)
    : impl_(std::make_unique<Impl>(engine, std::move(endpoint), std::move(stateFile))) {}

Service::~Service() = default;

bool Service::startLocal() {
    std::lock_guard lock(impl_->mutex);
    try {
        if (impl_->failed || !impl_->station || !impl_->station->startLocal([this] { impl_->save(); }))
            return false;
        impl_->save();
        return true;
    } catch (...) {
        impl_->engine.injectFault();
        throw;
    }
}

void Service::stopLocal() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->station) {
        impl_->station->stopLocal();
        impl_->save();
    }
}

} // namespace kafka
