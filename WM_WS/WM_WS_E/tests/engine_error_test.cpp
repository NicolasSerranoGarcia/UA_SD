#include "engine/engine.hpp"

#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <thread>
#include <type_traits>

static_assert(std::is_same_v<decltype(Engine{}.start()), std::expected<void, Engine::Error>>);

TEST(Engine, CannotStartBeforeMonitorActivation) {
    Engine engine;
    EXPECT_FALSE(engine.start().has_value());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
}

TEST(Engine, RejectsInvalidFlowWithoutStarting) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.timeToShutdown(), std::chrono::seconds{0});
    for (float invalid : {-1.f, 91.f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        SCOPED_TRACE(invalid);
        const auto result = engine.start(invalid);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().type, Engine::ErrorType::InvalidFlow);
        ASSERT_TRUE(result.error().invalidFlow.has_value());
        EXPECT_EQ(result.error().invalidFlow->maximum, internal::Valve::maxFlux);
        const auto requested = result.error().invalidFlow->requested;
        if (std::isnan(invalid))
            EXPECT_TRUE(std::isnan(requested));
        else
            EXPECT_EQ(requested, invalid);
        EXPECT_FALSE(engine.isRunning());
        EXPECT_EQ(engine.measureFlow(), 0);
    }
    EXPECT_TRUE(engine.start(40).has_value());
}

TEST(Engine, StartsStopsAndRestarts) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start(40).has_value());
    EXPECT_TRUE(engine.isRunning());
    EXPECT_GT(engine.timeToShutdown(), std::chrono::seconds{0});
    EXPECT_GE(engine.measureFlow(), 39.2f);
    EXPECT_LE(engine.measureFlow(), 40);
    const auto repeated = engine.start(40);
    ASSERT_FALSE(repeated.has_value());
    EXPECT_EQ(repeated.error().type, Engine::ErrorType::AlreadyWatering);
    EXPECT_FALSE(repeated.error().invalidFlow.has_value());
    ASSERT_TRUE(engine.stop().has_value());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.timeToShutdown(), std::chrono::seconds{0});
    EXPECT_EQ(engine.measureFlow(), 0);
    const auto stopped = engine.stop();
    ASSERT_FALSE(stopped.has_value());
    EXPECT_EQ(stopped.error().type, Engine::ErrorType::AlreadyStopped);
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, StoppingDoesNotRepairFault) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start().has_value());
    engine.injectFault();
    EXPECT_FALSE(engine.checkHealth());
    EXPECT_EQ(engine.measureFlow(), 0);
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.isHealthy());
    const auto faulted = engine.start();
    ASSERT_FALSE(faulted.has_value());
    EXPECT_EQ(faulted.error().type, Engine::ErrorType::Fault);
    ASSERT_TRUE(engine.stop().has_value());
    EXPECT_FALSE(engine.isHealthy());
    EXPECT_EQ(engine.timeToShutdown(), std::chrono::seconds{0});
    engine.repair();
    EXPECT_TRUE(engine.isHealthy());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, BlockingStopsIrrigationAndUnblockingDoesNotRepairFault) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start().has_value());
    ASSERT_TRUE(engine.block(0).has_value());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    const auto blocked = engine.start();
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().type, Engine::ErrorType::BlockedByCentral);
    engine.injectFault();
    ASSERT_FALSE(engine.unblock(0).has_value());
    EXPECT_FALSE(engine.isHealthy());
    EXPECT_FALSE(engine.start().has_value());
}

TEST(Engine, RepairDoesNotRemoveCentralBlock) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.block(0).has_value());
    engine.injectFault();
    EXPECT_FALSE(engine.checkHealth());
    engine.repair();
    EXPECT_TRUE(engine.isHealthy());
    const auto blocked = engine.start();
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().type, Engine::ErrorType::BlockedByCentral);
    EXPECT_FALSE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.unblock(0).has_value());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, FaultCanBeInjectedWhileIdle) {
    Engine engine;
    engine.setMonitorConnected(true);
    engine.injectFault();
    EXPECT_FALSE(engine.checkHealth());
    EXPECT_FALSE(engine.isHealthy());
    EXPECT_FALSE(engine.start().has_value());
    engine.repair();
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, WorkerStopsExpiredIrrigation) {
    Engine engine(Engine::Config{.irrigationTimeoutLimit = 0});
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start().has_value());
    const auto waitLimit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (engine.isRunning() && std::chrono::steady_clock::now() < waitLimit)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
}

TEST(Engine, UsesConfiguredTimeout) {
    Engine engine(Engine::Config{.irrigationTimeoutLimit = 120});
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start().has_value());
    EXPECT_GT(engine.timeToShutdown(), std::chrono::seconds{118});
    EXPECT_LE(engine.timeToShutdown(), std::chrono::seconds{120});
}

TEST(Engine, MonitorLossStopsAndRequiresFreshActivation) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    engine.setMonitorConnected(false);
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.isActivated());
    EXPECT_FALSE(engine.canPublish());
    EXPECT_EQ(engine.measureFlow(), 0);
    EXPECT_FALSE(engine.start());
    EXPECT_FALSE(engine.unblock(0));
    engine.setMonitorConnected(true);
    EXPECT_FALSE(engine.start());
    EXPECT_FALSE(engine.canPublish());
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_TRUE(engine.canPublish());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_TRUE(engine.start());
}

TEST(Engine, RepairNeedsActivationAndNeverResumesWatering) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    engine.injectFault();
    EXPECT_FALSE(engine.checkHealth());
    EXPECT_FALSE(engine.isActivated());
    EXPECT_FALSE(engine.unblock(0));
    engine.repair();
    EXPECT_TRUE(engine.isHealthy());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.start());
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_TRUE(engine.start());
}

TEST(Engine, BlockRequiresExternalActivationAndDoesNotResumeSession) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    ASSERT_TRUE(engine.block(0));
    EXPECT_FALSE(engine.isRunning());
    EXPECT_FALSE(engine.isActivated());
    EXPECT_FALSE(engine.start());
    EXPECT_TRUE(engine.canPublish());
    ASSERT_TRUE(engine.unblock(0));
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    EXPECT_TRUE(engine.start());
}

TEST(Engine, MonitorActivationCannotClearCentralBlock) {
    Engine engine;
    EXPECT_FALSE(engine.activateFromMonitor());
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.block(0));
    EXPECT_FALSE(engine.activateFromMonitor());
    EXPECT_FALSE(engine.isActivated());
    ASSERT_TRUE(engine.unblock(0));
    EXPECT_TRUE(engine.isActivated());
}

TEST(Engine, MonitorLossPreservesWaterAlreadyDelivered) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start(60));
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    engine.setMonitorConnected(false);
    const auto stopped = engine.reading();
    EXPECT_GT(stopped.volumeLiters, 0);
    EXPECT_GT(stopped.elapsedSeconds, 0);
    EXPECT_EQ(stopped.flowLitersPerMinute, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    EXPECT_EQ(engine.reading().volumeLiters, stopped.volumeLiters);
}

TEST(Engine, FastRepairCannotSkipFaultNotificationBeforeActivation) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    engine.injectFault();
    engine.repair();
    EXPECT_TRUE(engine.isHealthy());
    EXPECT_FALSE(engine.activateFromMonitor());
    EXPECT_FALSE(engine.checkHealth());
    EXPECT_TRUE(engine.checkHealth());
    EXPECT_TRUE(engine.activateFromMonitor());
}

TEST(Engine, LeakCanReportFinalTotalsButRequiresNapReactivationBeforeStarting) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    engine.injectFault();
    EXPECT_TRUE(engine.canPublish());
    EXPECT_FALSE(engine.checkHealth());
    engine.repair();
    EXPECT_FALSE(engine.unblock(0));
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_TRUE(engine.start());
}

TEST(Engine, CentralLossKeepsCurrentIrrigationButRejectsNewStarts) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    engine.setCentralAvailable(false);
    EXPECT_TRUE(engine.isRunning());
    EXPECT_TRUE(engine.canPublish());
    ASSERT_TRUE(engine.stop());
    const auto refused = engine.start();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().type, Engine::ErrorType::CentralUnavailable);
    ASSERT_TRUE(engine.activateFromMonitor());
    EXPECT_TRUE(engine.isCentralAvailable());
    EXPECT_TRUE(engine.start());
}

TEST(Engine, CentralRecoveryPreservesAdministrativeBlock) {
    Engine engine;
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.block(0));
    engine.setCentralAvailable(false);
    EXPECT_FALSE(engine.unblock(0));
    EXPECT_FALSE(engine.activateFromMonitor());
    EXPECT_TRUE(engine.isCentralAvailable());
    EXPECT_TRUE(engine.isBlocked());
    EXPECT_FALSE(engine.start());
    EXPECT_TRUE(engine.unblock(0));
}

TEST(Engine, DurableMeterExposesOnlyCommittedProgressAndStopsImmediately) {
    Engine engine;
    engine.enableDurableMetering();
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    const auto sample = engine.sampleMeter();
    EXPECT_GT(sample.reading.volumeLiters, 0);
    EXPECT_EQ(engine.reading().volumeLiters, 0);
    engine.commitMeter(sample);
    EXPECT_EQ(engine.reading().volumeLiters, sample.reading.volumeLiters);
    engine.setMonitorConnected(false);
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    const auto final = engine.sampleMeter();
    engine.commitMeter(final);
    EXPECT_GE(engine.reading().volumeLiters, sample.reading.volumeLiters);
    engine.setMonitorConnected(true);
    ASSERT_TRUE(engine.activateFromMonitor());
    ASSERT_TRUE(engine.start());
    engine.commitMeter(final); // An old session cannot overwrite a new irrigation.
    EXPECT_EQ(engine.reading().volumeLiters, 0);
}
