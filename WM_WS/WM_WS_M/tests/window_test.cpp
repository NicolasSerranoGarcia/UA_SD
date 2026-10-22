#include "gui/window.hpp"

#include <QApplication>
#include <QLabel>
#include <gtest/gtest.h>

namespace {

app::Options options() {
    using namespace std::chrono_literals;
    return {"station-1", ninttp::IPv4Endpoint::loopback(8080), "central.example", 9000,
            "spain", 100ms, 100ms, 100ms, 100ms};
}

QString value(const gui::Window &window, const char *name) {
    const auto *label = window.findChild<QLabel *>(name);
    return label ? label->text() : QString{};
}

TEST(MonitorWindow, DisplaysConfigurationBeforeFirstUpdate) {
    gui::Window window(options());
    EXPECT_EQ(value(window, "wsId"), "station-1");
    EXPECT_EQ(value(window, "location"), "spain");
    EXPECT_EQ(value(window, "centralHost"), "central.example:9000");
    EXPECT_EQ(value(window, "engineEndpoint"), "127.0.0.1:8080");
    EXPECT_EQ(value(window, "engineHealth"), "Unknown");
}

TEST(MonitorWindow, DisplaysLiveHealthAndLiteralErrorText) {
    gui::Window window(options());
    app::Status status;
    status.phase = app::Phase::Retrying;
    status.centralEndpoint = ninttp::IPv4Endpoint::loopback(9000);
    status.lastError = "<b>connection lost</b>";
    window.updateStatus(status);
    EXPECT_EQ(value(window, "phase"), "Reconnecting to Engine");
    EXPECT_EQ(value(window, "centralEndpoint"), "127.0.0.1:9000");
    EXPECT_EQ(value(window, "lastError"), "<b>connection lost</b>");
    EXPECT_EQ(window.findChild<QLabel *>("lastError")->textFormat(), Qt::PlainText);
    status.phase = app::Phase::Monitoring;
    status.health = app::Health::Ok;
    status.lastHealth = std::chrono::system_clock::now();
    window.updateStatus(status);
    EXPECT_EQ(value(window, "engineHealth"), "OK");
    EXPECT_NE(value(window, "lastHealth"), "No health response yet");
    status.health = app::Health::Ko;
    window.updateStatus(status);
    EXPECT_EQ(value(window, "engineHealth"), "KO");
}

TEST(MonitorWindow, CloseWaitsForWorkerAndRequestsStopOnlyOnce) {
    gui::Window window(options());
    int requests = 0;
    window.setStopHandler([&] { ++requests; });
    window.show();
    EXPECT_FALSE(window.close());
    EXPECT_FALSE(window.close());
    EXPECT_EQ(requests, 1);
    EXPECT_TRUE(window.isVisible());
    window.monitoringFinished();
    EXPECT_FALSE(window.isVisible());
}

TEST(MonitorWindow, FailureRemainsVisibleUntilDismissed) {
    gui::Window window(options());
    window.show();
    app::Status status;
    status.phase = app::Phase::Failed;
    status.lastError = "registration rejected";
    window.updateStatus(status);
    window.monitoringFinished();
    EXPECT_TRUE(window.isVisible());
    EXPECT_EQ(value(window, "phase"), "Startup failed");
    EXPECT_TRUE(window.close());
}

} // namespace

int main(int argc, char *argv[]) {
    testing::InitGoogleTest(&argc, argv);
    // Test discovery needs no display or platform plugin.
    if (testing::GTEST_FLAG(list_tests))
        return RUN_ALL_TESTS();
    QApplication application(argc, argv);
    return RUN_ALL_TESTS();
}
