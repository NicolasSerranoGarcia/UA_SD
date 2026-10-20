#include "engine/engine.hpp"

#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <thread>
#include <type_traits>

static_assert(std::is_same_v<decltype(Engine{}.start()), std::expected<void, Engine::Error>>);

TEST(Engine, RejectsInvalidFlowWithoutStarting) {
    Engine engine;
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
    ASSERT_TRUE(engine.start(40).has_value());
    EXPECT_TRUE(engine.isRunning());
    EXPECT_GT(engine.timeToShutdown(), std::chrono::seconds{0});
    EXPECT_EQ(engine.measureFlow(), 40);
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
    ASSERT_TRUE(engine.start().has_value());
    engine.injectFault();
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
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, BlockingStopsIrrigationAndUnblockingDoesNotRepairFault) {
    Engine engine;
    ASSERT_TRUE(engine.start().has_value());
    ASSERT_TRUE(engine.block(0).has_value());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    const auto blocked = engine.start();
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().type, Engine::ErrorType::BlockedByCentral);
    engine.injectFault();
    ASSERT_TRUE(engine.unblock(0).has_value());
    EXPECT_FALSE(engine.isHealthy());
    EXPECT_FALSE(engine.start().has_value());
}

TEST(Engine, RepairDoesNotRemoveCentralBlock) {
    Engine engine;
    ASSERT_TRUE(engine.block(0).has_value());
    engine.injectFault();
    engine.repair();
    EXPECT_TRUE(engine.isHealthy());
    const auto blocked = engine.start();
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().type, Engine::ErrorType::BlockedByCentral);
    ASSERT_TRUE(engine.unblock(0).has_value());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, FaultCanBeInjectedWhileIdle) {
    Engine engine;
    engine.injectFault();
    EXPECT_FALSE(engine.isHealthy());
    EXPECT_FALSE(engine.start().has_value());
    engine.repair();
    EXPECT_TRUE(engine.start().has_value());
}

TEST(Engine, WorkerStopsExpiredIrrigation) {
    Engine engine(Engine::Config{.irrigationTimeoutLimit = 0});
    ASSERT_TRUE(engine.start().has_value());
    const auto waitLimit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (engine.isRunning() && std::chrono::steady_clock::now() < waitLimit)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(engine.isRunning());
    EXPECT_EQ(engine.measureFlow(), 0);
}

TEST(Engine, UsesConfiguredTimeout) {
    Engine engine(Engine::Config{.irrigationTimeoutLimit = 120});
    ASSERT_TRUE(engine.start().has_value());
    EXPECT_GT(engine.timeToShutdown(), std::chrono::seconds{118});
    EXPECT_LE(engine.timeToShutdown(), std::chrono::seconds{120});
}
