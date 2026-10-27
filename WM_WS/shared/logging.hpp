#pragma once

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ninttp/platform_traits.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>

namespace logging {

// Installed before workers start and removed only after they have joined.
inline std::string currentPath;

inline std::osyncstream record() {
    std::osyncstream output(std::clog);
    output << std::unitbuf;
    const auto now = std::time(nullptr);
    std::tm utc{};
#if NINTTP_PLATFORM_WINDOWS == 1
    gmtime_s(&utc, &now);
#elif NINTTP_PLATFORM_UNIX == 1
    gmtime_r(&now, &utc);
#endif
    output << '[' << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ") << "] ";
    return output;
}

class Session {
  public:
    explicit Session(const std::filesystem::path &path) {
        if (!currentPath.empty())
            throw std::logic_error("A logging session is already active");
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        file_.open(path, std::ios::app);
        if (!file_)
            throw std::runtime_error("Cannot open log file '" + path.string() + "'. Make sure this location is writable.");
        previous_ = std::clog.rdbuf(file_.rdbuf());
        currentPath = path.string();
        record() << "Application logging started" << std::endl;
    }

    ~Session() {
        std::clog.flush();
        std::clog.rdbuf(previous_);
        currentPath.clear();
    }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

  private:
    std::ofstream file_;
    std::streambuf *previous_ = nullptr;
};

inline void fatal(std::string_view module, std::string_view summary,
                  std::string_view details, std::string_view nextStep) {
    if (!currentPath.empty())
        record() << '[' << module << "][FATAL] " << summary << " Details: " << details
                 << " Next step: " << nextStep << std::endl;
    std::osyncstream terminal(std::cerr);
    terminal << "\n" << module << " - FATAL ERROR: " << summary
             << "\nTechnical details: " << details << "\nNext step: " << nextStep << '\n';
    if (!currentPath.empty())
        terminal << "Log file: " << currentPath << '\n';
    terminal << std::flush;
}

} // namespace logging
