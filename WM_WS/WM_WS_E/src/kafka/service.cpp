#include "logging.hpp"
#include "kafka/service.hpp"

#include "kafka/station.hpp"
#include "kafka/journal.hpp"
#include <iostream>
#include <atomic>
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

struct Service::Impl : RdKafka::DeliveryReportCb, RdKafka::EventCb {
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
    std::string failureMessage;
    std::atomic<bool> brokerFatal = false;
    std::mutex brokerErrorMutex;
    std::string brokerError;
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

    void event_cb(RdKafka::Event &event) override {
        logging::record() << "[Engine][Kafka][" << event.fac() << "] " << event.str() << std::endl;
        if (event.type() == RdKafka::Event::EVENT_ERROR && event.fatal()) {
            std::lock_guard lock(brokerErrorMutex);
            brokerError = "Kafka error " + std::to_string(static_cast<int>(event.err())) + ": " + event.str();
            brokerFatal.store(true);
        }
    }

    void configureLogging(RdKafka::Conf &configuration) {
        std::string error;
        if (configuration.set("event_cb", static_cast<RdKafka::EventCb *>(this), error) != RdKafka::Conf::CONF_OK)
            throw std::runtime_error(error);
    }

    void save() {
        const auto sample = engine.sampleMeter();
        journal.save(station->checkpoint(sample.reading));
        engine.commitMeter(sample);
        nextCheckpoint = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
    }

    void initialize(const std::string &id) {
        station = std::make_unique<Station>(engine, id);
        engine.enableDurableMetering();
        const auto latest = journal.load();
        if (latest) {
            if (const auto restored = station->restore(*latest); !restored)
                throw std::runtime_error(restored.error());
        }
        save();
        std::string error;
        std::unique_ptr<RdKafka::Conf> configuration{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        configureLogging(*configuration);
        configure(*configuration, "bootstrap.servers", endpoint);
        configure(*configuration, "enable.idempotence", "true");
        configure(*configuration, "message.timeout.ms", "3000");
        configure(*configuration, "socket.timeout.ms", "1000");
        if (configuration->set("dr_cb", static_cast<RdKafka::DeliveryReportCb *>(this), error) != RdKafka::Conf::CONF_OK)
            throw std::runtime_error(error);
        producer.reset(RdKafka::Producer::create(configuration.get(), error));
        if (!producer)
            throw std::runtime_error(error);
        configuration.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
        configureLogging(*configuration);
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
            logging::record() << "[Engine][Kafka] ws=" << id
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
                            failed = true;
                            failureMessage = "Kafka routing requires a numeric station ID; received '" + id + "'.";
                            logging::fatal("Engine", "Kafka messaging is disabled for this station.", failureMessage,
                                           "Restart Engine and Monitor with a numeric --ws-id.");
                            return;
                        }
                        initialize(id);
                    }
                    if (station) {
                        producer->poll(0);
                        if (brokerFatal.load()) {
                            std::lock_guard lock(brokerErrorMutex);
                            throw std::runtime_error(brokerError);
                        }
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
                                        logging::record() << "[Engine][Kafka] " << handled.error() << '\n';
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
            failureMessage = error.what();
            if (producer)
                (void)producer->purge(RdKafka::Producer::PURGE_QUEUE | RdKafka::Producer::PURGE_INFLIGHT);
            engine.injectFault();
            logging::fatal("Engine", "Kafka service stopped; irrigation has been stopped safely.", error.what(),
                           "Check the broker configuration and checkpoint storage, then restart Engine.");
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

std::optional<std::string> Service::failureReason() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->failed ? std::optional{impl_->failureMessage} : std::nullopt;
}

} // namespace kafka
