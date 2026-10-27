#include "kafka/journal.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <ninttp/platform_traits.hpp>
#include <stdexcept>
#if NINTTP_PLATFORM_WINDOWS == 1
#include <io.h>
#elif NINTTP_PLATFORM_UNIX == 1
#include <fcntl.h>
#include <unistd.h>
#else
#error "Station checkpoints require a Windows or Unix file sync implementation"
#endif

namespace kafka {

Journal::Journal(std::filesystem::path path) : path_(std::move(path)) {}

std::optional<boost::json::object> Journal::load() {
    revision_ = 0;
    std::optional<boost::json::object> latest;
    bool existing = false;
    for (const auto *suffix : {".0", ".1"}) {
        auto path = path_;
        path += suffix;
        if (!std::filesystem::exists(path))
            continue;
        existing = true;
        std::ifstream file(path, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(file), {}};
        boost::system::error_code error;
        const auto record = boost::json::parse(contents, error);
        if (error || !record.is_object())
            continue;
        const auto &wrapper = record.as_object();
        const auto *revision = wrapper.if_contains("revision");
        const auto *state = wrapper.if_contains("state");
        if (!revision || !revision->is_number() || !state || !state->is_object())
            continue;
        try {
            const auto number = revision->to_number<std::uint64_t>();
            if (number > revision_) {
                revision_ = number;
                latest = state->as_object();
            }
        } catch (const std::exception &) {
            // An incomplete or corrupt checkpoint must not replace the other slot.
        }
    }
    if (existing && !latest)
        throw std::runtime_error("no valid station checkpoint: " + path_.string());
    return latest;
}

void Journal::save(const boost::json::object &state) {
    const auto revision = revision_ + 1;
    auto path = path_;
    path += revision % 2 ? ".1" : ".0";
    const auto contents = boost::json::serialize(boost::json::object{{"revision", revision}, {"state", state}});
#if NINTTP_PLATFORM_WINDOWS == 1
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file{_wfopen(path.c_str(), L"wb"), &std::fclose};
#else
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file{std::fopen(path.c_str(), "wb"), &std::fclose};
#endif
    if (!file || std::fwrite(contents.data(), 1, contents.size(), file.get()) != contents.size() ||
        std::fflush(file.get()) != 0)
        throw std::runtime_error("cannot write station checkpoint: " + path.string());
#if NINTTP_PLATFORM_WINDOWS == 1
    const auto synced = _commit(_fileno(file.get()));
#else
    const auto synced = fsync(fileno(file.get()));
#endif
    if (synced != 0)
        throw std::runtime_error("cannot sync station checkpoint: " + path.string());
#if NINTTP_PLATFORM_UNIX == 1
    const auto parent = path.parent_path().empty() ? std::filesystem::path{"."} : path.parent_path();
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0)
        throw std::runtime_error("cannot open checkpoint directory: " + parent.string());
    const auto directorySynced = fsync(directory);
    close(directory);
    if (directorySynced != 0)
        throw std::runtime_error("cannot sync checkpoint directory: " + parent.string());
#endif
    revision_ = revision;
}

} // namespace kafka
