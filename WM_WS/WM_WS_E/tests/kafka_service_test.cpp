#include "engine/engine.hpp"
#include "kafka/service.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafka_mock.h>
#include <librdkafka/rdkafkacpp.h>
#include <memory>
#include <thread>

namespace {
using namespace std::chrono_literals;

TEST(KafkaService, RoutesRecordsAndDefersFinishUntilMonitorRecovery) {
    std::string error;
    std::unique_ptr<RdKafka::Conf> config{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
    std::unique_ptr<RdKafka::Producer> host{RdKafka::Producer::create(config.get(), error)};
    ASSERT_TRUE(host) << error;
    const auto cluster = std::unique_ptr<rd_kafka_mock_cluster_t, decltype(&rd_kafka_mock_cluster_destroy)>{
        rd_kafka_mock_cluster_new(host->c_ptr(), 1), &rd_kafka_mock_cluster_destroy};
    ASSERT_TRUE(cluster);
    for (const auto &[name, count] : {std::pair{"central-ops", 2}, {"telemetry", 3},
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
    engine.setWsId("1");
    {
        kafka::Service service(engine, bootstrap, journal.string());
        EXPECT_FALSE(engine.isActivated());
        engine.setMonitorConnected(true);
        ASSERT_TRUE(engine.activateFromMonitor());
        std::string pendingPayload;
        const auto publish = [&] {
            ASSERT_EQ(central->produce("central-ops", 1, RdKafka::Producer::RK_MSG_COPY,
                pendingPayload.data(), pendingPayload.size(), nullptr, 0, 0, nullptr), RdKafka::ERR_NO_ERROR);
            ASSERT_EQ(central->flush(2000), RdKafka::ERR_NO_ERROR);
        };
        const auto send = [&](std::string_view type, int operation) {
            const auto payload = boost::json::serialize(boost::json::object{
                {"type", type}, {"station_id", 1}, {"operation_code", operation},
                {"operator_id", 7}, {"duration", 60}});
            pendingPayload = payload;
            publish();
        };
        const auto receive = [&](std::string_view topic, int operation = 0) {
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
                auto value = boost::json::parse(std::string_view{
                    static_cast<const char *>(message->payload()), message->len()}).as_object();
                EXPECT_EQ(message->partition(), 1) << message->topic_name();
                if (message->topic_name() == topic &&
                    (!operation || value.at("operation_code").to_number<int>() == operation)) {
                    if (operation)
                        pendingPayload.clear();
                    return value;
                }
            }
            ADD_FAILURE() << "No Kafka output on topic " << topic;
            return boost::json::object{};
        };
        send("IRRIGATION", 1);
        ASSERT_EQ(receive("ws-start-conf", 1).at("status").as_string(), "OK");
        const auto sample = receive("telemetry");
        EXPECT_EQ(sample.size(), 3);
        EXPECT_EQ(sample.at("flux_l_min").to_number<double>(), 40.0);
        ASSERT_EQ(sample.at("operator_id").as_int64(), 7);
        EXPECT_TRUE(engine.isRunning());
        engine.setMonitorConnected(false);
        EXPECT_FALSE(engine.isActivated());
        // Drain records already delivered before disconnect; no new records may follow.
        std::this_thread::sleep_for(200ms);
        while (true) {
            std::unique_ptr<RdKafka::Message> message{observer->consume(100)};
            if (message->err() == RdKafka::ERR__TIMED_OUT)
                break;
        }
        std::unique_ptr<RdKafka::Message> quiet{observer->consume(1200)};
        EXPECT_EQ(quiet->err(), RdKafka::ERR__TIMED_OUT);
        engine.setMonitorConnected(true);
        ASSERT_TRUE(engine.activateFromMonitor());
        const auto final = receive("ws-finish-conf");
        EXPECT_EQ(final.at("status").as_string(), "FINISHED");
        EXPECT_EQ(final.size(), 5);
        EXPECT_GT(final.at("total_liters").to_number<double>(), 0);
        EXPECT_FALSE(engine.isRunning());
        send("BLOCK", 2);
        ASSERT_EQ(receive("ws-start-conf", 2).at("status").as_string(), "OK");
        EXPECT_FALSE(engine.isActivated());
        send("ACTIVATE", 3);
        ASSERT_EQ(receive("ws-start-conf", 3).at("status").as_string(), "OK");
        EXPECT_TRUE(engine.isActivated());
        EXPECT_FALSE(engine.isRunning());
    }
    EXPECT_EQ(observer->close(), RdKafka::ERR_NO_ERROR);
}

} // namespace
