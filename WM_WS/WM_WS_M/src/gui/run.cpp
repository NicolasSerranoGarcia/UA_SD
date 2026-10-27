#include "gui/run.hpp"

#include "app/app.hpp"
#include "gui/window.hpp"
#include "logging.hpp"
#include <QApplication>
#include <QMetaObject>
#include <exception>
#include <iostream>
#include <ninttp/socket/error/socket_error.hpp>
#include <thread>

namespace gui {

int run(int argc, char *argv[], const app::Options &options) {
    QApplication application(argc, argv);
    Window window(options);
    int result = 0;
    std::jthread worker([&](std::stop_token stop) {
        app::Status latest;
        const auto publish = [&](const app::Status &status) {
            latest = status;
            QMetaObject::invokeMethod(&window, [&window, status] { window.updateStatus(status); },
                                      Qt::QueuedConnection);
        };
        const auto failed = [&](const char *reason) {
            logging::fatal("Monitor", "The monitoring worker stopped unexpectedly.", reason,
                           "Check the log and Engine/Central connectivity, then restart Monitor.");
            latest.phase = app::Phase::Failed;
            latest.lastError = reason;
            publish(latest);
            result = 1;
        };
        try {
            result = app::run(options, stop, publish);
        } catch (const ninttp::SocketError &error) {
            failed(error.what());
        } catch (const std::exception &error) {
            failed(error.what());
        }
        QMetaObject::invokeMethod(&window, [&window] { window.monitoringFinished(); }, Qt::QueuedConnection);
    });
    window.setStopHandler([&] { worker.request_stop(); });
    window.show();
    const auto uiResult = application.exec();
    worker.request_stop();
    worker.join();
    return uiResult == 0 ? result : uiResult;
}

} // namespace gui
