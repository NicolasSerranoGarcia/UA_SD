#include "engine/flow_meter/flow_meter.hpp"
#include "engine/valve/valve.hpp"

#include <gtest/gtest.h>
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
