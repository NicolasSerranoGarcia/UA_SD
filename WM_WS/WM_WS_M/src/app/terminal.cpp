#include "app/app.hpp"

#include "console.hpp"
#include "logging.hpp"
#include <atomic>
#include <iostream>
#include <mutex>
#include <ninttp/socket/error/socket_error.hpp>
#include <syncstream>
#include <thread>

namespace app {
namespace {

std::string_view phaseName(Phase phase) {
    switch (phase) {
    case Phase::Resolving: return "Looking up Central's network address";
    case Phase::Registering: return "Registering this station with Central";
    case Phase::Monitoring: return "Checking Engine and reporting its health to Central";
    case Phase::Retrying: return "Engine is unavailable. Trying to reconnect.";
    case Phase::Failed: return "Monitor could not continue. See the error below.";
    case Phase::Stopped: return "Monitor has stopped";
    }
    return "Waiting for a status update";
}

void show(const Options &options, const Status &status) {
    std::osyncstream output(std::cout);
    const auto address = options.engineEndpoint.addressHostOrder();
    output << "\n========== Watering station | Monitor ==========\n"
           << "Station: " << options.wsId << " | Location: " << options.location
           << "\nState: " << phaseName(status.phase)
           << "\nEngine health: " << (status.health == Health::Ok ? "Healthy" : status.health == Health::Ko ? "Fault detected - repair is needed" : "Waiting for a health response")
           << "\nEngine: " << ((address >> 24) & 0xff) << '.' << ((address >> 16) & 0xff)
           << '.' << ((address >> 8) & 0xff) << '.' << (address & 0xff)
           << ':' << options.engineEndpoint.portHostOrder()
           << "\nCentral: " << options.centralHostname << ':' << options.centralPort
           << "\nActivity log: " << options.logFile;
    if (!status.lastError.empty())
        output << "\nA connection problem was recorded. See the activity log for details.";
    output << "\n\n  [P / 1] Status of this station"
           << "\n  [H]     Help / show this menu"
           << "\n  [Q / 0] Quit Monitor"
           << "\n\nChoose a letter or number and press Enter."
           << "\nYou can also type: status, help, quit."
           << "\nUse the Engine menu to start or stop watering, simulate a fault, or repair it."
           << "\nWhen attached to Docker, press Ctrl+P then Ctrl+Q to leave Monitor running.\n> " << std::flush;
}

} // namespace

int runTerminal(const Options &options) {
    std::mutex mutex;
    Status latest;
    std::atomic<bool> finished = false;
    int result = 0;
    bool consoleFailed = false;
    show(options, latest);
    std::jthread worker([&](std::stop_token stop) {
        const auto publish = [&](const Status &status) {
            bool changed;
            {
                std::lock_guard lock(mutex);
                changed = latest.phase != status.phase || latest.health != status.health;
                latest = status;
            }
            if (changed && status.phase != Phase::Stopped)
                show(options, status);
        };
        const auto fatal = [&](const char *reason) {
            logging::fatal("Monitor", "The monitoring worker could not continue.", reason,
                           "Check the log and Engine/Central connectivity, then restart Monitor.");
            result = 1;
        };
        try {
            result = run(options, stop, publish);
        } catch (const ninttp::SocketError &error) {
            fatal(error.what());
        } catch (const std::exception &error) {
            fatal(error.what());
        }
        finished.store(true);
    });
    console::Input input(console::inputEcho());
    bool inputOpen = true;
    while (!finished.load()) {
        if (!inputOpen) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            continue;
        }
        const auto command = input.read(std::chrono::milliseconds{100});
        if (!command) {
            logging::fatal("Monitor", "The interactive console could not read input.", command.error().message(),
                           "Start Monitor with an interactive terminal or inspect its container stdin settings.");
            consoleFailed = true;
            break;
        }
        if (command->kind == console::Kind::Closed) {
            inputOpen = false;
            logging::record() << "[Monitor][Console] input closed; monitoring continues" << std::endl;
            continue;
        }
        if (command->kind == console::Kind::Waiting)
            continue;
        const auto &key = command->text;
        if (key == "q" || key == "0" || key == "quit") {
            std::osyncstream(std::cout) << "Stopping Monitor. Engine will stop watering when it detects that Monitor has disconnected.\n" << std::flush;
            break;
        }
        if (command->kind == console::Kind::Invalid ||
            (key != "p" && key != "1" && key != "status" && key != "h" && key != "help"))
            std::osyncstream(std::cout) << "Unknown or invalid option. Choose one of the commands below.\n";
        Status snapshot;
        {
            std::lock_guard lock(mutex);
            snapshot = latest;
        }
        show(options, snapshot);
    }
    worker.request_stop();
    worker.join();
    return consoleFailed ? 1 : result;
}

} // namespace app
