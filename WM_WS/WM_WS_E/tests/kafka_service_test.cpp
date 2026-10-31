#include "engine/engine.hpp"
#include "kafka/service.hpp"
#include "kafka/protocol.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafka_mock.h>
#include <librdkafka/rdkafkacpp.h>
#include <memory>
#include <thread>

namespace {
using namespace std::chrono_literals;

TEST(KafkaService, RoutesRecordsAndDefersFinishUntilRecovery) {
    for (const bool brokerFailure : {false, true}) {
        SCOPED_TRACE(brokerFailure ? "Broker outage" : "Monitor outage");
        std::string error;
        std::unique_ptr<RdKafka::Conf> config{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
        std::unique_ptr<RdKafka::Producer> host{RdKafka::Producer::create(config.get(), error)};
        ASSERT_TRUE(host) << error;
        const auto cluster = std::unique_ptr<rd_kafka_mock_cluster_t, decltype(&rd_kafka_mock_cluster_destroy)>{
            rd_kafka_mock_cluster_new(host->c_ptr(), 1), &rd_kafka_mock_cluster_destroy};
        ASSERT_TRUE(cluster);
        for (const auto &[name, count] : {std::pair{"central-ops", 3}, {"telemetry", 3},
                                        {"ws-start-conf", 2}, {"ws-finish-conf", 2}})
            ASSERT_EQ(rd_kafka_mock_topic_create(cluster.get(), name, count, 1), RD_KAFKA_RESP_ERR_NO_ERROR);
        const std::string bootstrap = rd_kafka_mock_cluster_bootstraps(cluster.get());
        ASSERT_EQ(config->set("bootstrap.servers", bootstrap, error), RdKafka::Conf::CONF_OK);
        std::unique_ptr<RdKafka::Producer> central{RdKafka::Producer::create(config.get(), error)};
        ASSERT_TRUE(central) << error;
        ASSERT_EQ(config->set("group.id", "station-service-test", error), RdKafka::Conf::CONF_OK);
        std::unique_ptr<RdKafka::KafkaConsumer> observer{RdKafka::KafkaConsumer::create(config.get(), error)};
        ASSERT_TRUE(observer) << error;
        std::vector<RdKafka::TopicPartition *> assignment;
        for (const auto &[name, count] : {std::pair{"telemetry", 3}, {"ws-start-conf", 2}, {"ws-finish-conf", 2}})
            for (int partition = 0; partition < count; ++partition)
                assignment.push_back(RdKafka::TopicPartition::create(name, partition, RdKafka::Topic::OFFSET_BEGINNING));
        ASSERT_EQ(observer->assign(assignment), RdKafka::ERR_NO_ERROR);
        RdKafka::TopicPartition::destroy(assignment);

        const auto journal = std::filesystem::temp_directory_path() /
            ("wm-ws-kafka-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jsonl");
        struct JournalCleanup {
            std::filesystem::path path;
            ~JournalCleanup() {
                std::error_code ignored;
                std::filesystem::remove(path.string() + ".0", ignored);
                std::filesystem::remove(path.string() + ".1", ignored);
            }
        } cleanup{journal};
        Engine engine;
        engine.setWsId("01");
        {
            kafka::Service service(engine, bootstrap, journal.string());
            EXPECT_FALSE(engine.isActivated());
            engine.setMonitorConnected(true);
            ASSERT_TRUE(engine.activateFromMonitor());
            std::string pendingPayload;
            const auto publish = [&] {
                const std::string key = "1";
                ASSERT_EQ(central->produce("central-ops", RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                    pendingPayload.data(), pendingPayload.size(), key.data(), key.size(), 0, nullptr), RdKafka::ERR_NO_ERROR);
                ASSERT_EQ(central->flush(2000), RdKafka::ERR_NO_ERROR);
            };
            const auto send = [&](std::string_view type, int operation) {
                const auto payload = boost::json::serialize(boost::json::object{
                    {"type", type}, {"station_id", 1}, {"operation_code", operation},
                    {"operator_id", 7}, {"duration", 60}});
                pendingPayload = payload;
                publish();
            };
            const auto receive = [&](std::string_view topic, std::string_view operation = {}) {
                const auto limit = std::chrono::steady_clock::now() + 8s;
                auto retry = std::chrono::steady_clock::now() + 500ms;
                while (std::chrono::steady_clock::now() < limit) {
                    if (!pendingPayload.empty() && std::chrono::steady_clock::now() >= retry) {
                        publish();
                        retry = std::chrono::steady_clock::now() + 500ms;
                    }
                    std::unique_ptr<RdKafka::Message> message{observer->consume(100)};
                    if (message->err() != RdKafka::ERR_NO_ERROR)
                        continue;
                    const std::string_view payload{
                        static_cast<const char *>(message->payload()), message->len()};
                    auto value = message->topic_name() == "ws-start-conf" ? kafka::parseCommand(payload) :
                        boost::json::parse(payload).as_object();
                    EXPECT_EQ(message->partition(), 1) << message->topic_name();
                    if (message->topic_name() == topic &&
                        (operation.empty() || kafka::operationKey(value.at("operation_code")) == operation)) {
                        if (!operation.empty()) {
                            EXPECT_NE(payload.find("\"operation_code\":" + std::string{operation}), std::string_view::npos);
                            pendingPayload.clear();
                        }
                        return value;
                    }
                }
                ADD_FAILURE() << "No Kafka output on topic " << topic;
                return boost::json::object{};
            };
            send("IRRIGATION", 1);
            ASSERT_EQ(receive("ws-start-conf", "1").at("status").as_string(), "OK");
            const auto sample = receive("telemetry");
            EXPECT_EQ(sample.size(), 3);
            EXPECT_GE(sample.at("flux_l_min").to_number<double>(), 39.2 - 0.00001);
            EXPECT_LE(sample.at("flux_l_min").to_number<double>(), 40);
            ASSERT_EQ(sample.at("operator_id").as_int64(), 7);
            EXPECT_TRUE(engine.isRunning());
            if (brokerFailure) {
                ASSERT_EQ(rd_kafka_mock_broker_set_down(cluster.get(), -1), RD_KAFKA_RESP_ERR_NO_ERROR);
                service.stopLocal();
            } else {
                engine.setMonitorConnected(false);
                EXPECT_FALSE(engine.isActivated());
            }
            // Drain records already delivered before disconnect; no new records may follow.
            std::this_thread::sleep_for(200ms);
            while (true) {
                std::unique_ptr<RdKafka::Message> message{observer->consume(100)};
                if (message->err() == RdKafka::ERR__TIMED_OUT)
                    break;
            }
            std::unique_ptr<RdKafka::Message> quiet{observer->consume(1200)};
            if (brokerFailure) {
                EXPECT_NE(quiet->err(), RdKafka::ERR_NO_ERROR);
                ASSERT_EQ(rd_kafka_mock_broker_set_up(cluster.get(), -1), RD_KAFKA_RESP_ERR_NO_ERROR);
            } else {
                EXPECT_EQ(quiet->err(), RdKafka::ERR__TIMED_OUT);
                engine.setMonitorConnected(true);
                ASSERT_TRUE(engine.activateFromMonitor());
            }
            const auto final = receive("ws-finish-conf");
            EXPECT_EQ(final.at("status").as_string(), "FINISHED");
            EXPECT_EQ(final.size(), 5);
            EXPECT_GT(final.at("total_liters").to_number<double>(), 0);
            EXPECT_FALSE(engine.isRunning());
            send("BLOCK", 2);
            ASSERT_EQ(receive("ws-start-conf", "2").at("status").as_string(), "OK");
            EXPECT_FALSE(engine.isActivated());
            send("ACTIVATE", 3);
            ASSERT_EQ(receive("ws-start-conf", "3").at("status").as_string(), "OK");
            EXPECT_TRUE(engine.isActivated());
            EXPECT_FALSE(engine.isRunning());
            constexpr std::string_view uuid = "340282366920938463463374607431768211455";
            pendingPayload = R"({"type":"BLOCK","station_id":1,"operation_code":)" +
                std::string{uuid} + R"(,"operator_id":7,"duration":null})";
            publish();
            ASSERT_EQ(receive("ws-start-conf", uuid).at("status").as_string(), "OK");
            EXPECT_FALSE(engine.isActivated());
        }
        EXPECT_EQ(observer->close(), RdKafka::ERR_NO_ERROR);
    }
}

TEST(KafkaService, StorageFailureStopsTheValveAndReportsTheError) {
    const auto path = std::filesystem::temp_directory_path() /
        ("wm-storage-failure-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    { std::ofstream file(path); file << "not a directory"; }
    Engine engine;
    engine.setWsId("1");
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    kafka::Service service(engine, "127.0.0.1:1", (path / "state.jsonl").string());
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!service.failureReason()) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.isHealthy());
    EXPECT_FALSE(engine.start());
    EXPECT_FALSE(service.failureReason()->empty());
}

TEST(KafkaService, ManualStartReachesCentralWithBrokerLatency) {
    std::string error;
    std::unique_ptr<RdKafka::Conf> config{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
    std::unique_ptr<RdKafka::Producer> host{RdKafka::Producer::create(config.get(), error)};
    ASSERT_TRUE(host) << error;
    const auto cluster = std::unique_ptr<rd_kafka_mock_cluster_t, decltype(&rd_kafka_mock_cluster_destroy)>{
        rd_kafka_mock_cluster_new(host->c_ptr(), 1), &rd_kafka_mock_cluster_destroy};
    ASSERT_TRUE(cluster);
    for (const auto &[name, count] : {std::pair{"central-ops", 4}, {"telemetry", 3},
                                    {"ws-start-conf", 2}, {"ws-finish-conf", 2}})
        ASSERT_EQ(rd_kafka_mock_topic_create(cluster.get(), name, count, 1), RD_KAFKA_RESP_ERR_NO_ERROR);
    ASSERT_EQ(rd_kafka_mock_broker_set_rtt(cluster.get(), -1, 300), RD_KAFKA_RESP_ERR_NO_ERROR);
    const std::string bootstrap = rd_kafka_mock_cluster_bootstraps(cluster.get());
    ASSERT_EQ(config->set("bootstrap.servers", bootstrap, error), RdKafka::Conf::CONF_OK);
    std::unique_ptr<RdKafka::Producer> central{RdKafka::Producer::create(config.get(), error)};
    ASSERT_TRUE(central) << error;
    ASSERT_EQ(config->set("group.id", "manual-start-latency-test", error), RdKafka::Conf::CONF_OK);
    std::unique_ptr<RdKafka::KafkaConsumer> observer{RdKafka::KafkaConsumer::create(config.get(), error)};
    ASSERT_TRUE(observer) << error;
    std::vector<RdKafka::TopicPartition *> assignment;
    for (int partition = 0; partition < 2; ++partition)
        assignment.push_back(RdKafka::TopicPartition::create("ws-start-conf", partition, RdKafka::Topic::OFFSET_BEGINNING));
    ASSERT_EQ(observer->assign(assignment), RdKafka::ERR_NO_ERROR);
    RdKafka::TopicPartition::destroy(assignment);

    const auto path = std::filesystem::temp_directory_path() /
        ("wm-manual-latency-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jsonl");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path.string() + ".0", ignored);
            std::filesystem::remove(path.string() + ".1", ignored);
        }
    } cleanup{path};
    Engine engine;
    engine.setWsId("1");
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    {
        kafka::Service service(engine, bootstrap, path.string());
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!service.startLocal()) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            std::this_thread::sleep_for(20ms);
        }
        bool confirmed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            std::unique_ptr<RdKafka::Message> message{observer->consume(100)};
            if (message->err() != RdKafka::ERR_NO_ERROR)
                continue;
            const auto value = boost::json::parse(std::string_view{
                static_cast<const char *>(message->payload()), message->len()}).as_object();
            const boost::json::object expected{{"station_id", 1}, {"operation_code", nullptr},
                {"status", "OK"}, {"reason", nullptr}};
            EXPECT_EQ(value, expected);
            EXPECT_EQ(message->partition(), 1);
            confirmed = true;
            break;
        }
        EXPECT_TRUE(engine.isRunning());
        ASSERT_TRUE(confirmed) << "Manual irrigation confirmation did not reach Central's topic";
        const auto command = [&](std::string_view type, int code) {
            boost::json::object value{{"station_id", 1}, {"operator_id", nullptr},
                {"type", type}, {"operation_code", code}};
            if (type == "IRRIGATION")
                value["duration"] = nullptr;
            auto payload = boost::json::serialize(value);
            const std::string key = "1";
            if (central->produce("central-ops", RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                payload.data(), payload.size(), key.data(), key.size(), 0, nullptr) != RdKafka::ERR_NO_ERROR ||
                central->flush(5000) != RdKafka::ERR_NO_ERROR)
                return false;
            const auto limit = std::chrono::steady_clock::now() + 8s;
            while (std::chrono::steady_clock::now() < limit) {
                std::unique_ptr<RdKafka::Message> message{observer->consume(100)};
                if (message->err() != RdKafka::ERR_NO_ERROR)
                    continue;
                const auto reply = boost::json::parse(std::string_view{
                    static_cast<const char *>(message->payload()), message->len()}).as_object();
                if (!reply.at("operation_code").is_null() && reply.at("operation_code").to_number<int>() == code)
                    return reply.at("status") == "OK";
            }
            return false;
        };
        ASSERT_TRUE(command("BLOCK", 2));
        EXPECT_TRUE(engine.isBlocked());
        EXPECT_FALSE(engine.isRunning());
        ASSERT_TRUE(command("ACTIVATE", 3));
        EXPECT_TRUE(engine.isActivated());
        ASSERT_TRUE(command("IRRIGATION", 4));
        EXPECT_TRUE(engine.isRunning());
        ASSERT_EQ(rd_kafka_mock_broker_set_rtt(cluster.get(), -1, 1000), RD_KAFKA_RESP_ERR_NO_ERROR);
        const auto controlDeadline = std::chrono::steady_clock::now() + 7s;
        while (std::chrono::steady_clock::now() < controlDeadline) {
            const auto requested = std::chrono::steady_clock::now();
            service.stopLocal();
            const auto elapsed = std::chrono::steady_clock::now() - requested;
            EXPECT_FALSE(engine.isRunning());
            EXPECT_LT(elapsed, 500ms) << "Broker metadata retrieval blocked the local stop control";
            if (elapsed >= 500ms)
                break;
            std::this_thread::sleep_for(20ms);
        }
        ASSERT_EQ(rd_kafka_mock_broker_set_rtt(cluster.get(), -1, 0), RD_KAFKA_RESP_ERR_NO_ERROR);
        engine.setMonitorConnected(false);
    }
    EXPECT_EQ(observer->close(), RdKafka::ERR_NO_ERROR);
}

} // namespace
