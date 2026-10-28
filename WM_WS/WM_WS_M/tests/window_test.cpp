#include "gui/window.hpp"

#include <QApplication>
#include <QLabel>
#include <QDialog>
#include <QKeyEvent>
#include <QShortcut>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
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
    EXPECT_EQ(value(window, "engineHealth"), "Waiting for a health response");
}

TEST(MonitorWindow, DisplaysEngineHostnameAndCentralAvailability) {
    auto configuration = options();
    configuration.engineHostname = "engine";
    gui::Window window(configuration);
    EXPECT_EQ(value(window, "engineEndpoint"), "engine:8080");
    app::Status status;
    status.centralAvailable = true;
    window.updateStatus(status);
    EXPECT_EQ(value(window, "centralState"), "Available");
    status.centralAvailable = false;
    window.updateStatus(status);
    EXPECT_TRUE(value(window, "centralState").contains("Unavailable"));
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
    EXPECT_EQ(value(window, "engineHealth"), "Healthy");
    EXPECT_NE(value(window, "lastHealth"), "No health response yet");
    status.health = app::Health::Ko;
    window.updateStatus(status);
    EXPECT_EQ(value(window, "engineHealth"), "Fault detected - repair needed");
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
    EXPECT_EQ(value(window, "phase"), "Monitor could not continue");
    EXPECT_TRUE(window.close());
}

TEST(MonitorWindow, HelpDoesNotInterruptLiveStatus) {
    gui::Window window(options());
    auto *help = window.findChild<QPushButton *>("helpButton");
    ASSERT_NE(help, nullptr);

    help->click();
    auto *dialog = window.findChild<QDialog *>("helpDialog");
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_FALSE(dialog->isModal());
    app::Status status;
    status.phase = app::Phase::Retrying;
    window.updateStatus(status);
    EXPECT_EQ(value(window, "phase"), "Reconnecting to Engine");
    help->click();
    EXPECT_EQ(window.findChildren<QDialog *>("helpDialog").size(), 1);
}

TEST(MonitorWindow, ActivityLogCanBeOpenedAndRefreshed) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto configuration = options();
    configuration.logFile = (directory.path() + "/monitor.log").toStdString();
    QFile file(QString::fromStdString(configuration.logFile));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("First health check\n");
    file.close();
    gui::Window window(configuration);
    auto *open = window.findChild<QPushButton *>("logButton");
    ASSERT_NE(open, nullptr);
    open->click();
    auto *dialog = window.findChild<QDialog *>("logDialog");
    ASSERT_NE(dialog, nullptr);
    EXPECT_FALSE(dialog->isModal());
    auto *text = dialog->findChild<QPlainTextEdit *>("logContents");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->isReadOnly());
    EXPECT_EQ(text->toPlainText(), "First health check\n");
    ASSERT_TRUE(file.open(QIODevice::Append));
    file.write("<b>Next health check</b>\n");
    file.close();
    auto *refresh = dialog->findChild<QPushButton *>("refreshLogButton");
    ASSERT_NE(refresh, nullptr);
    refresh->click();
    EXPECT_TRUE(text->toPlainText().contains("<b>Next health check</b>"));
    open->click();
    EXPECT_EQ(window.findChildren<QDialog *>("logDialog").size(), 1);
}

TEST(MonitorWindow, MissingLogShowsAnActionableMessage) {
    QTemporaryDir directory;
    auto configuration = options();
    configuration.logFile = (directory.path() + "/missing.log").toStdString();
    gui::Window window(configuration);
    auto *open = window.findChild<QPushButton *>("logButton");
    ASSERT_NE(open, nullptr);
    open->click();
    auto *text = window.findChild<QPlainTextEdit *>("logContents");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->toPlainText().contains("could not be opened"));
    EXPECT_TRUE(text->toPlainText().contains("Refresh"));
}

TEST(MonitorWindow, QuitButtonUsesTheGracefulCloseHandshake) {
    gui::Window window(options());
    int requests = 0;
    window.setStopHandler([&] { ++requests; });
    window.show();
    auto *quit = window.findChild<QPushButton *>("quitButton");
    ASSERT_NE(quit, nullptr);
    quit->click();
    EXPECT_EQ(requests, 1);
    EXPECT_FALSE(quit->isEnabled());
    EXPECT_TRUE(window.isVisible());
    EXPECT_TRUE(value(window, "phase").contains("Stopping"));
    window.close();
    EXPECT_EQ(requests, 1);
    window.monitoringFinished();
    EXPECT_FALSE(window.isVisible());
}

TEST(MonitorWindow, CentralErrorsAreVisibleAsPlainText) {
    gui::Window window(options());
    app::Status status;
    status.centralError = "<b>Central refused the connection</b>";
    window.updateStatus(status);
    auto *label = window.findChild<QLabel *>("centralError");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "<b>Central refused the connection</b>");
    EXPECT_EQ(label->textFormat(), Qt::PlainText);
    status.centralError.clear();
    status.centralAvailable = true;
    window.updateStatus(status);
    EXPECT_EQ(label->text(), "None");
}

TEST(MonitorWindow, ShortcutsWorkWhileHelpAndLogHaveFocus) {
    gui::Window window(options());
    int requests = 0;
    window.setStopHandler([&] { ++requests; });
    window.show();
    window.activateWindow();
    QApplication::processEvents();
    const auto key = [](QWidget *target, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers);
        QApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, code, modifiers);
        QApplication::sendEvent(target, &release);
        QApplication::processEvents();
    };
    key(&window, Qt::Key_F1);
    auto *help = window.findChild<QDialog *>("helpDialog");
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->isVisible());
    help->activateWindow();
    QApplication::processEvents();
    key(help, Qt::Key_L, Qt::ControlModifier);
    auto *log = window.findChild<QDialog *>("logDialog");
    ASSERT_NE(log, nullptr);
    EXPECT_TRUE(log->isVisible());
    log->activateWindow();
    QApplication::processEvents();
    key(log, Qt::Key_Q, Qt::ControlModifier);
    EXPECT_EQ(requests, 1);
    window.monitoringFinished();
    EXPECT_FALSE(log->isVisible());
    EXPECT_FALSE(help->isVisible());
}

TEST(MonitorWindow, LogPreviewIsBoundedAndOmitsPartialFirstLine) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto configuration = options();
    configuration.logFile = (directory.path() + "/large.log").toStdString();
    QFile file(QString::fromStdString(configuration.logFile));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(70000, 'x'));
    file.write("\nMost recent health check\n");
    file.close();
    gui::Window window(configuration);
    auto *open = window.findChild<QPushButton *>("logButton");
    ASSERT_NE(open, nullptr);
    open->click();
    auto *text = window.findChild<QPlainTextEdit *>("logContents");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->toPlainText(), "Most recent health check\n");
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
