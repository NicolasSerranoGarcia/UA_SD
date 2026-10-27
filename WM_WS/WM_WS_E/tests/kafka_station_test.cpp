#include "kafka/station.hpp"
#include "kafka/journal.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

namespace {

TEST(KafkaRouting, UsesNumericRecipientModuloWithoutOverflow) {
    EXPECT_EQ(*kafka::partitionFor("7", 3), 1);
    EXPECT_EQ(*kafka::partitionFor("10", 3), 1);
    EXPECT_EQ(*kafka::partitionFor("9999999999999999999999999999999999999999999999999999999999999999", 3), 0);
    EXPECT_FALSE(kafka::partitionFor("station-1", 3));
    EXPECT_FALSE(kafka::partitionFor("1", 0));
    EXPECT_FALSE(kafka::partitionFor("-1", 3));
}

class KafkaStation : public testing::Test {
  protected:
    Engine engine;
    kafka::Station station{engine, "1"};

    void SetUp() override {
        engine.setMonitorConnected(true);
        ASSERT_TRUE(engine.activateFromMonitor());
    }

    std::string command(std::string_view type, int code = 1, int target = 1) {
        return boost::json::serialize(boost::json::object{
            {"type", type}, {"operation_code", code}, {"station_id", target},
            {"operator_id", 7}, {"duration", 60}});
    }
};

TEST_F(KafkaStation, UsesReadmeOperationsAndExactConfirmationShape) {
    ASSERT_TRUE(station.handle(command("IRRIGATION")));
    ASSERT_TRUE(engine.isRunning());
    ASSERT_EQ(station.events().size(), 1);
    const boost::json::object expected{{"station_id", 1}, {"operation_code", 1}, {"status", "OK"}, {"reason", nullptr}};
    EXPECT_EQ(station.events().front().value, expected);
    ASSERT_TRUE(station.handle(command("BLOCK", 2)));
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.isActivated());
    ASSERT_TRUE(station.handle(command("IRRIGATION", 3)));
    EXPECT_EQ(station.events().back().value.at("status").as_string(), "KO");
    ASSERT_TRUE(station.handle(command("ACTIVATE", 4)));
    EXPECT_TRUE(engine.isActivated());
    EXPECT_FALSE(engine.isRunning());
    ASSERT_TRUE(station.handle(command("IRRIGATION", 5)));
    EXPECT_TRUE(engine.isRunning());
}

TEST_F(KafkaStation, IgnoresOtherRecipientsSharingPartition) {
    ASSERT_TRUE(station.handle(command("BLOCK", 1, 3)));
    EXPECT_TRUE(engine.isActivated());
    EXPECT_TRUE(station.events().empty());
}

TEST_F(KafkaStation, MonitorLossPreservesReadmeFinishForRecovery) {
    ASSERT_TRUE(station.handle(command("IRRIGATION")));
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    station.events().clear();
    engine.setMonitorConnected(false);
    station.tick();
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.canPublish());
    ASSERT_EQ(station.events().size(), 1);
    const auto &event = station.events().front();
    EXPECT_EQ(event.topic, "ws-finish-conf");
    EXPECT_EQ(event.value.size(), 5);
    EXPECT_EQ(event.value.at("station_id").to_number<int>(), 1);
    EXPECT_EQ(event.value.at("status").as_string(), "FINISHED");
    EXPECT_GT(event.value.at("total_liters").to_number<double>(), 0);
    EXPECT_GT(event.value.at("total_time").to_number<double>(), 0);
    EXPECT_EQ(event.value.at("operator_id").to_number<int>(), 7);
}

TEST_F(KafkaStation, TelemetryUsesReadmeFieldsAndOwnerModuloRouting) {
    ASSERT_TRUE(station.handle(command("IRRIGATION")));
    station.events().clear();
    station.tick();
    ASSERT_EQ(station.events().size(), 1);
    const auto &event = station.events().front();
    EXPECT_EQ(event.topic, "telemetry");
    EXPECT_EQ(event.recipient, "7");
    const boost::json::object expected{{"station_id", 1}, {"flux_l_min", 40.0}, {"operator_id", 7}};
    EXPECT_EQ(event.value, expected);
}

TEST_F(KafkaStation, NullOrOmittedDurationMeansIndefiniteAndDuplicateDoesNotRestart) {
    auto request = boost::json::parse(command("IRRIGATION")).as_object();
    request["duration"] = nullptr;
    ASSERT_TRUE(station.handle(boost::json::serialize(request)));
    EXPECT_GT(engine.timeToShutdown(), std::chrono::hours{24 * 365});
    station.stopLocal();
    ASSERT_TRUE(station.handle(boost::json::serialize(request)));
    EXPECT_FALSE(engine.isRunning());
    request["operation_code"] = 2;
    request.erase("duration");
    ASSERT_TRUE(station.handle(boost::json::serialize(request)));
    EXPECT_TRUE(engine.isRunning());
}

TEST_F(KafkaStation, RejectsUnknownCommandsAndReusedOperationCodes) {
    EXPECT_FALSE(station.handle("{invalid"));
    EXPECT_FALSE(station.handle(command("PREPARE")));
    ASSERT_TRUE(station.handle(command("BLOCK")));
    EXPECT_FALSE(station.handle(command("ACTIVATE")));
    EXPECT_FALSE(engine.isActivated());
}

TEST_F(KafkaStation, RestoresPendingFinalAndDoesNotReplayCompletedOperation) {
    ASSERT_TRUE(station.handle(command("IRRIGATION")));
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    const auto checkpoint = station.checkpoint();
    Engine recoveredEngine;
    kafka::Station recovered(recoveredEngine, "1");
    ASSERT_TRUE(recovered.restore(checkpoint));
    EXPECT_FALSE(recoveredEngine.isRunning());
    EXPECT_FALSE(recoveredEngine.canPublish());
    ASSERT_EQ(recovered.events().back().topic, "ws-finish-conf");
    EXPECT_GT(recovered.events().back().value.at("total_liters").to_number<double>(), 0);
    recoveredEngine.setMonitorConnected(true);
    ASSERT_TRUE(recoveredEngine.activateFromMonitor());
    ASSERT_TRUE(recovered.handle(command("IRRIGATION")));
    EXPECT_FALSE(recoveredEngine.isRunning());
}

TEST_F(KafkaStation, NormalizesNumericStationIdentity) {
    kafka::Station padded(engine, "01");
    ASSERT_TRUE(padded.handle(command("ACTIVATE")));
    ASSERT_EQ(padded.events().size(), 1);
    EXPECT_EQ(padded.events().front().value.at("status").as_string(), "OK");
}

TEST_F(KafkaStation, ManualStartHasNullOperationCodeAndDoesNotBypassBlock) {
    ASSERT_TRUE(station.startLocal());
    EXPECT_TRUE(engine.isRunning());
    const boost::json::object expected{{"station_id", 1}, {"operation_code", nullptr},
        {"status", "OK"}, {"reason", nullptr}};
    ASSERT_EQ(station.events().front().value, expected);
    station.stopLocal();
    EXPECT_TRUE(station.events().back().value.at("operator_id").is_null());
    ASSERT_TRUE(station.handle(command("BLOCK", 2)));
    const auto count = station.events().size();
    EXPECT_FALSE(station.startLocal());
    EXPECT_EQ(station.events().size(), count);
    EXPECT_FALSE(engine.isRunning());
}

TEST_F(KafkaStation, PendingIntentIsDurableBeforeValveOpensAndDoesNotReplay) {
    boost::json::object intent;
    ASSERT_TRUE(station.handle(command("IRRIGATION"), [&] {
        EXPECT_FALSE(engine.isRunning());
        intent = station.checkpoint();
    }));
    EXPECT_TRUE(engine.isRunning());
    Engine restoredEngine;
    kafka::Station restored(restoredEngine, "1");
    ASSERT_TRUE(restored.restore(intent));
    restoredEngine.setMonitorConnected(true);
    ASSERT_TRUE(restoredEngine.activateFromMonitor());
    ASSERT_TRUE(restored.handle(command("IRRIGATION")));
    EXPECT_FALSE(restoredEngine.isRunning());
    EXPECT_EQ(restored.events().back().value.at("status").as_string(), "KO");
}

TEST_F(KafkaStation, InvalidAddressedOperationReceivesKoWithoutLosingOriginalOutcome) {
    auto invalid = boost::json::parse(command("IRRIGATION")).as_object();
    invalid["duration"] = 0;
    EXPECT_FALSE(station.handle(boost::json::serialize(invalid)));
    ASSERT_FALSE(station.events().empty());
    EXPECT_EQ(station.events().back().value.at("operation_code").to_number<int>(), 1);
    EXPECT_EQ(station.events().back().value.at("status").as_string(), "KO");
    ASSERT_TRUE(station.handle(command("BLOCK", 2)));
    EXPECT_FALSE(station.handle(command("ACTIVATE", 2)));
    ASSERT_TRUE(station.handle(command("BLOCK", 2)));
    EXPECT_EQ(station.events().back().value.at("status").as_string(), "OK");
    EXPECT_FALSE(engine.isActivated());
}

TEST_F(KafkaStation, NewStartIntentDoesNotReusePreviousIrrigationTotals) {
    ASSERT_TRUE(station.handle(command("IRRIGATION", 1)));
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    station.stopLocal();
    ASSERT_GT(engine.reading().volumeLiters, 0);
    boost::json::object pending;
    ASSERT_TRUE(station.handle(command("IRRIGATION", 2), [&] { pending = station.checkpoint(); }));
    Engine restoredEngine;
    kafka::Station restored(restoredEngine, "1");
    ASSERT_TRUE(restored.restore(pending));
    EXPECT_EQ(restored.events().back().value.at("total_liters").to_number<double>(), 0);
    EXPECT_EQ(restored.events().back().value.at("total_time").to_number<double>(), 0);
    station.stopLocal();
    ASSERT_TRUE(station.startLocal([&] { pending = station.checkpoint(); }));
    Engine restoredLocalEngine;
    kafka::Station restoredLocal(restoredLocalEngine, "1");
    ASSERT_TRUE(restoredLocal.restore(pending));
    EXPECT_EQ(restoredLocal.events().back().value.at("total_liters").to_number<double>(), 0);
}

TEST_F(KafkaStation, InterruptedActivationKeepsAdministrativeBlock) {
    ASSERT_TRUE(station.handle(command("BLOCK", 1)));
    boost::json::object pending;
    ASSERT_TRUE(station.handle(command("ACTIVATE", 2), [&] { pending = station.checkpoint(); }));
    Engine restoredEngine;
    kafka::Station restored(restoredEngine, "1");
    ASSERT_TRUE(restored.restore(pending));
    restoredEngine.setMonitorConnected(true);
    EXPECT_FALSE(restoredEngine.activateFromMonitor());
    EXPECT_FALSE(restoredEngine.start());
    ASSERT_TRUE(restored.handle(command("ACTIVATE", 2)));
    EXPECT_EQ(restored.events().back().value.at("status").as_string(), "KO");
    EXPECT_FALSE(restoredEngine.isActivated());
}

TEST(KafkaJournal, KeepsTwoCheckpointsAndSurvivesTornLatestWrite) {
    const auto path = std::filesystem::temp_directory_path() /
        ("wm-ws-journal-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path.string() + ".0", ignored);
            std::filesystem::remove(path.string() + ".1", ignored);
        }
    } cleanup{path};
    kafka::Journal journal(path);
    EXPECT_FALSE(journal.load());
    for (int count = 1; count <= 20; ++count)
        journal.save({{"counter", count}});
    EXPECT_LT(std::filesystem::file_size(path.string() + ".0"), 256);
    EXPECT_LT(std::filesystem::file_size(path.string() + ".1"), 256);
    kafka::Journal recovered(path);
    ASSERT_TRUE(recovered.load());
    std::ofstream(path.string() + ".0", std::ios::trunc) << "{incomplete";
    kafka::Journal torn(path);
    const auto prior = torn.load();
    ASSERT_TRUE(prior);
    EXPECT_EQ(prior->at("counter").as_int64(), 19);
}

TEST(KafkaJournal, CreatesStateDirectoriesAndLoadsSavedData) {
    const auto directory = std::filesystem::temp_directory_path() /
        ("wm-state-directory-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    } cleanup{directory};
    const auto path = directory / "state" / "station.jsonl";
    kafka::Journal journal(path);
    EXPECT_FALSE(journal.load());
    const boost::json::object saved{{"station_id", 1}};
    journal.save(saved);
    kafka::Journal recovered(path);
    const auto loaded = recovered.load();
    ASSERT_TRUE(loaded);
    EXPECT_EQ(*loaded, saved);
}

} // namespace
