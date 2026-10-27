#include "console.hpp"
#include "logging.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace {

TEST(ConsoleInput, DispatchesOnlyCompleteCommandsAndNormalizesCase) {
    console::Input input;
    for (char letter : std::string{" START "})
        EXPECT_EQ(input.feed(letter).kind, console::Kind::Waiting);
    const auto command = input.feed('\n');
    EXPECT_EQ(command.kind, console::Kind::Ready);
    EXPECT_EQ(command.text, "start");
    EXPECT_EQ(input.feed('\n').kind, console::Kind::Waiting);
}

TEST(ConsoleInput, OverlongInputIsDiscardedRatherThanExecutingItsSuffix) {
    console::Input input;
    for (int count = 0; count < 80; ++count)
        (void)input.feed('x');
    (void)input.feed('s');
    EXPECT_EQ(input.feed('\n').kind, console::Kind::Invalid);
    (void)input.feed('p');
    EXPECT_EQ(input.feed('\n').text, "p");
}

TEST(FileLogging, FlushesRoutineMessagesAndMirrorsOnlyFatalErrorsToTerminal) {
    const auto path = std::filesystem::temp_directory_path() /
        ("wm-logging-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    std::ostringstream terminal;
    struct Restore {
        std::streambuf *previous;
        ~Restore() { std::cerr.rdbuf(previous); }
    } restore{std::cerr.rdbuf(terminal.rdbuf())};
    {
        logging::Session log(path);
        logging::record() << "routine health check\n";
        EXPECT_TRUE(terminal.str().empty());
        std::ifstream immediate(path);
        const std::string written{std::istreambuf_iterator<char>(immediate), {}};
        EXPECT_NE(written.find("routine health check"), std::string::npos);
        logging::fatal("Engine", "Server stopped", "port occupied", "Select another port");
    }
    EXPECT_EQ(terminal.str().find("routine health check"), std::string::npos);
    EXPECT_NE(terminal.str().find("FATAL"), std::string::npos);
    EXPECT_NE(terminal.str().find("Next step: Select another port"), std::string::npos);
    EXPECT_TRUE(logging::currentPath.empty());
    std::ifstream file(path);
    const std::string contents{std::istreambuf_iterator<char>(file), {}};
    EXPECT_NE(contents.find("routine health check"), std::string::npos);
    EXPECT_NE(contents.find("port occupied"), std::string::npos);
}

TEST(FileLogging, CreatesLogDirectoriesAndAppendsOnRestart) {
    const auto directory = std::filesystem::temp_directory_path() /
        ("wm-log-directory-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    } cleanup{directory};
    const auto path = directory / "log" / "engine.log";
    for (int count = 0; count < 2; ++count) {
        logging::Session session(path);
        logging::record() << "Application run " << count << '\n';
    }
    std::ifstream file(path);
    const std::string contents{std::istreambuf_iterator<char>(file), {}};
    EXPECT_NE(contents.find("Application run 0"), std::string::npos);
    EXPECT_NE(contents.find("Application run 1"), std::string::npos);
}

} // namespace
