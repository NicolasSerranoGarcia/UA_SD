#include "engine/flow_meter/flow_meter.hpp"
#include "engine/valve/valve.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

TEST(Valve, AdjustsFlowWithinBounds) {
    internal::Valve valve;
    ASSERT_TRUE(valve.openBy(20).has_value());
    EXPECT_EQ(internal::FlowMeter::measure(valve), 20);
    ASSERT_TRUE(valve.closeBy(5).has_value());
    EXPECT_EQ(internal::FlowMeter::measure(valve), 15);
    EXPECT_FALSE(valve.closeBy(16).has_value());
    EXPECT_FALSE(valve.openBy(76).has_value());
    EXPECT_EQ(internal::FlowMeter::measure(valve), 15);
    valve.openFully();
    EXPECT_EQ(internal::FlowMeter::measure(valve), 90);
    valve.close();
    EXPECT_EQ(internal::FlowMeter::measure(valve), 0);
}

TEST(Valve, RejectsInvalidAdjustmentsAndStartupFlow) {
    internal::Valve valve(15);
    for (float invalid : {-1.f, 91.f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        SCOPED_TRACE(invalid);
        const auto result = valve.openBy(invalid);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().type, internal::Valve::ErrorType::InvalidFlow);
        EXPECT_FALSE(valve.closeBy(invalid).has_value());
        EXPECT_EQ(internal::FlowMeter::measure(valve), 15);
        EXPECT_THROW((void)internal::Valve{invalid}, std::invalid_argument);
    }
}

TEST(Valve, RepeatedUseDoesNotInjectPhysicalFaults) {
    internal::Valve valve;
    for (int i = 0; i < 100; ++i) {
        SCOPED_TRACE(i);
        valve.openFully();
        EXPECT_EQ(internal::FlowMeter::measure(valve), internal::Valve::maxFlux);
        valve.close();
        EXPECT_EQ(internal::FlowMeter::measure(valve), 0);
        valve.close();
        EXPECT_EQ(internal::FlowMeter::measure(valve), 0);
    }
}

TEST(FlowMeter, SimulatesSmallPressureChangesWithoutExceedingValveFlow) {
    internal::Valve valve(40);
    double lowest = 40;
    double highest = 0;
    for (int second = 0; second <= 30; ++second) {
        const double flow = internal::FlowMeter::measure(valve, second);
        EXPECT_GE(flow, 39.2 - 0.00001);
        EXPECT_LE(flow, 40);
        lowest = std::min(lowest, flow);
        highest = std::max(highest, flow);
    }
    EXPECT_GT(highest - lowest, 0.2);
    EXPECT_EQ(internal::FlowMeter::measure(valve, 3), internal::FlowMeter::measure(valve, 3));
    valve.openFully();
    EXPECT_LE(internal::FlowMeter::measure(valve, 4), internal::Valve::maxFlux);
    valve.close();
    EXPECT_EQ(internal::FlowMeter::measure(valve, 4), 0);
    EXPECT_EQ(internal::FlowMeter::volume(valve, 4), 0);
}

TEST(FlowMeter, DeliveredWaterIntegratesTheChangingFlow) {
    internal::Valve valve(40);
    EXPECT_EQ(internal::FlowMeter::volume(valve, 0), 0);
    double previous = 0;
    for (int second = 1; second <= 30; ++second) {
        const double volume = internal::FlowMeter::volume(valve, second);
        EXPECT_GT(volume, previous);
        EXPECT_GE(volume, 39.2 * second / 60);
        EXPECT_LE(volume, 40.0 * second / 60);
        const double step = 0.001;
        const double slope = (internal::FlowMeter::volume(valve, second + step) -
                              internal::FlowMeter::volume(valve, second - step)) / (2 * step);
        EXPECT_NEAR(slope * 60, internal::FlowMeter::measure(valve, second), 0.00001);
        previous = volume;
    }
}
