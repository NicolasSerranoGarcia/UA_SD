#include "app/terminal.hpp"

#include "console.hpp"
#include "engine/engine.hpp"
#include "kafka/service.hpp"
#include "logging.hpp"
#include <iomanip>
#include <iostream>
#include <ninttp/platform_traits.hpp>
#include <syncstream>
#if NINTTP_PLATFORM_WINDOWS == 1
#include <windows.h>
#elif NINTTP_PLATFORM_UNIX == 1
#include <unistd.h>
#endif

namespace app {
namespace {

bool enableCursorUpdates() {
#if NINTTP_PLATFORM_WINDOWS == 1
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode{};
    return GetConsoleMode(output, &mode) &&
           SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#elif NINTTP_PLATFORM_UNIX == 1
    return isatty(STDOUT_FILENO) == 1;
#else
    return false;
#endif
}

void measurements(std::ostream &output, const Engine::Reading &reading) {
    output << std::fixed << std::setprecision(2)
           << "Flow: " << reading.flowLitersPerMinute << " L/min"
           << " | Water delivered: " << reading.volumeLiters << " L"
           << " | Watering time: " << reading.elapsedSeconds << " s";
}

void refreshMeasurements(const Engine &engine, bool cursorUpdates) {
    std::osyncstream output(std::cout);
    // Update the measurement row above the prompt without moving the typing cursor.
    if (cursorUpdates)
        output << "\x1b" "7\x1b[1A\r\x1b[2K";
    measurements(output, engine.reading());
    if (cursorUpdates)
        output << "\x1b" "8";
    else
        output << '\n';
    output << std::flush;
}

std::string state(const Engine &engine) {
    if (!engine.isMonitorConnected()) return "Monitor is disconnected. Irrigation is unavailable.";
    if (!engine.isHealthy()) return "A fault has been detected. Repair it before watering.";
    if (engine.isRunning()) return "Watering in progress";
    return engine.isActivated() ? "Ready to water" : "Waiting for activation before watering";
}

void show(const Options &options, const Engine &engine, const kafka::Service &kafka) {
    const auto reading = engine.reading();
    const auto id = engine.wsId();
    std::osyncstream output(std::cout);
    output << "\n========== Watering station | Engine ==========\n"
           << "Station: " << (id.empty() ? "waiting for Monitor to provide the station ID" : id)
           << "\nState: " << state(engine)
           << "\nActivation: " << (engine.isActivated() ? "Active" : "Inactive - activate the station before starting")
           << "\nMessage broker (Kafka): " << options.queueEndpoint;
    if (kafka.failureReason())
        output << "\nMessaging is unavailable. Check the activity log and the error shown on screen.";
    output << "\nActivity log: " << options.logFile
           << "\nRecovery data: " << options.stateFile << ".0 and .1"
           << "\n\n  [S / 1] Start irrigation (until stopped)"
           << "\n  [E / 2] Stop irrigation"
           << "\n  [F / 3] Simulate a fault (stops watering)"
           << "\n  [R / 4] Repair the fault"
           << "\n  [P / 5] Status and latest measurements"
           << "\n  [H]     Help / show this menu"
           << "\n  [Q / 0] Quit Engine"
           << "\n\nChoose a letter or number and press Enter."
           << "\nYou can also type: start, stop, fault, repair, status, help, quit."
           << "\nWhen attached to Docker, press Ctrl+P then Ctrl+Q to leave Engine running."
           << "\nMeasurements:\n";
    measurements(output, reading);
    output << "\n> " << std::flush;
}

} // namespace

void readConsole(std::stop_token stop, Engine &engine, kafka::Service &kafka,
                 const Options &options, std::stop_source &shutdown, std::atomic<bool> &failed) {
    try {
        console::Input input;
        const bool cursorUpdates = enableCursorUpdates();
        show(options, engine, kafka);
        auto previousState = state(engine);
        auto nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (!stop.stop_requested() && !shutdown.stop_requested()) {
            const auto command = input.read(std::chrono::milliseconds{100});
            if (!command)
                throw std::system_error(command.error(), "reading terminal input");
            if (command->kind == console::Kind::Closed) {
                logging::record() << "[Engine][Console] input closed; health service continues" << std::endl;
                return;
            }
            if (command->kind == console::Kind::Waiting) {
                const auto now = std::chrono::steady_clock::now();
                const auto current = state(engine);
                if (current != previousState) {
                    previousState = current;
                    show(options, engine, kafka);
                    nextRefresh = now + std::chrono::seconds{1};
                } else if (engine.isRunning() && now >= nextRefresh) {
                    refreshMeasurements(engine, cursorUpdates);
                    nextRefresh = now + std::chrono::seconds{1};
                }
                continue;
            }
            const auto &key = command->text;
            std::string feedback;
            if (command->kind == console::Kind::Invalid) feedback = command->text;
            else if (key == "s" || key == "1" || key == "start")
                feedback = kafka.startLocal() ? "Irrigation has started." : "Irrigation could not start. Check that Monitor is connected, the station is active, no fault is present, and watering is not already running.";
            else if (key == "e" || key == "2" || key == "stop") {
                kafka.stopLocal();
                feedback = "Irrigation is stopped. The final water measurements have been kept.";
            } else if (key == "f" || key == "3" || key == "fault") {
                engine.injectFault();
                feedback = "A fault has been simulated. Watering has stopped, and Monitor will report the fault to Central.";
            } else if (key == "r" || key == "4" || key == "repair") {
                engine.repair();
                feedback = "The fault has been repaired. Monitor will request activation again. If Central has blocked the station, request activation through Central before starting.";
            } else if (key == "q" || key == "0" || key == "quit") {
                kafka.stopLocal();
                std::osyncstream(std::cout) << "Stopping Engine. Saved recovery data will keep any messages still waiting to be sent.\n" << std::flush;
                shutdown.request_stop();
                return;
            } else if (key != "p" && key != "5" && key != "status" && key != "h" && key != "help")
                feedback = "Unknown option. Choose one of the commands below.";
            if (!feedback.empty()) {
                logging::record() << "[Engine][Console] " << feedback << std::endl;
                std::osyncstream(std::cout) << '\n' << feedback << '\n';
            }
            previousState = state(engine);
            show(options, engine, kafka);
            nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        }
    } catch (const std::exception &error) {
        failed.store(true);
        logging::fatal("Engine", "The interactive console could not continue.", error.what(),
                       "Check terminal access and checkpoint storage, then restart Engine.");
        shutdown.request_stop();
    }
}

} // namespace app
