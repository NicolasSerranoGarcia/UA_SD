#include "poll/poll.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <string_view>
#include <system_error>
#include <thread>

#if NINTTP_PLATFORM_UNIX == 1
#include <unistd.h>
#endif

namespace {

// Redirect only this test process's stdin; restore it before the next test.
class StdinPoll : public testing::Test {
  protected:
    void SetUp() override {
#if NINTTP_PLATFORM_WINDOWS == 1
        savedInput = ::GetStdHandle(STD_INPUT_HANDLE);
        ASSERT_TRUE(::CreatePipe(&reader, &writer, nullptr, 0));
        ASSERT_TRUE(::SetStdHandle(STD_INPUT_HANDLE, reader));
#else
        savedInput = ::dup(STDIN_FILENO);
        ASSERT_NE(savedInput, -1);
        ASSERT_EQ(::pipe(pipeEnds), 0);
        ASSERT_NE(::dup2(pipeEnds[0], STDIN_FILENO), -1);
#endif
    }

    void TearDown() override {
#if NINTTP_PLATFORM_WINDOWS == 1
        EXPECT_TRUE(::SetStdHandle(STD_INPUT_HANDLE, savedInput));
        if (reader)
            (void)::CloseHandle(reader);
        if (writer)
            (void)::CloseHandle(writer);
#else
        if (savedInput != -1) {
            (void)::dup2(savedInput, STDIN_FILENO);
            (void)::close(savedInput);
        }
        for (int fd : pipeEnds)
            if (fd != -1)
                (void)::close(fd);
#endif
    }

    void closeWriter() {
#if NINTTP_PLATFORM_WINDOWS == 1
        (void)::CloseHandle(writer);
        writer = nullptr;
#else
        (void)::close(pipeEnds[1]);
        pipeEnds[1] = -1;
#endif
    }

    bool writeInput(std::string_view text) {
#if NINTTP_PLATFORM_WINDOWS == 1
        DWORD count{};
        return ::WriteFile(writer, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) &&
               count == text.size();
#else
        return ::write(pipeEnds[1], text.data(), text.size()) == static_cast<ssize_t>(text.size());
#endif
    }

    void invalidateInput() {
#if NINTTP_PLATFORM_WINDOWS == 1
        ASSERT_TRUE(::SetStdHandle(STD_INPUT_HANDLE, INVALID_HANDLE_VALUE));
#else
        ASSERT_EQ(::close(STDIN_FILENO), 0);
#endif
    }

#if NINTTP_PLATFORM_WINDOWS == 1
    HANDLE savedInput = nullptr;
    HANDLE reader = nullptr;
    HANDLE writer = nullptr;
#else
    int savedInput = -1;
    int pipeEnds[2] = {-1, -1};
#endif
};

TEST_F(StdinPoll, TimesOutWithoutInput) {
    const auto result = poll::poll(std::chrono::milliseconds{10});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, poll::Status::Timeout);
}

TEST_F(StdinPoll, ReadsAvailableCharacterWithoutWaiting) {
    ASSERT_TRUE(writeInput("f"));
    const auto result = poll::poll(std::chrono::milliseconds{0});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, poll::Status::Input);
    EXPECT_EQ(result->character, 'f');
}

TEST_F(StdinPoll, WakesWhenInputArrives) {
    bool written = false;
    std::jthread writerThread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        written = writeInput("r");
    });
    const auto result = poll::poll(std::chrono::milliseconds{1000});
    writerThread.join();
    ASSERT_TRUE(written);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, poll::Status::Input);
    EXPECT_EQ(result->character, 'r');
}

TEST_F(StdinPoll, DrainsInputBeforeReportingEof) {
    ASSERT_TRUE(writeInput("fr"));
    closeWriter();
    for (const char expected : {'f', 'r'}) {
        const auto result = poll::poll(std::chrono::milliseconds{100});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->status, poll::Status::Input);
        EXPECT_EQ(result->character, expected);
    }
    const auto eof = poll::poll(std::chrono::milliseconds{100});
    ASSERT_TRUE(eof.has_value());
    EXPECT_EQ(eof->status, poll::Status::Closed);
}

TEST_F(StdinPoll, ReportsInvalidInputHandle) {
    invalidateInput();
    const auto result = poll::poll(std::chrono::milliseconds{0});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(static_cast<bool>(result.error()));
}

TEST_F(StdinPoll, RejectsNegativeTimeout) {
    const auto result = poll::poll(std::chrono::milliseconds{-1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::errc::invalid_argument);
}

} // namespace
