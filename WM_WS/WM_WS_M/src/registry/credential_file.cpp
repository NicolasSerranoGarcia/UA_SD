#include "credential_file.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <ninttp/platform_traits.hpp>
#include <stdexcept>

#if NINTTP_PLATFORM_WINDOWS == 1
#include <windows.h>
#include <wincrypt.h>
#elif NINTTP_PLATFORM_UNIX == 1
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace registry {
namespace {
constexpr std::size_t MAX_FILE = 8192;

[[noreturn]] void fileError() {
    throw std::runtime_error("Could not access the private station credential file.");
}

#if NINTTP_PLATFORM_WINDOWS == 1
std::string protect(const std::string &bytes, bool encrypt) {
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data()))};
    DATA_BLOB output{};
    const auto success = encrypt ?
        CryptProtectData(&input, L"WM Registry credential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) :
        CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!success)
        fileError();
    const std::unique_ptr<void, decltype(&LocalFree)> guard(output.pbData, LocalFree);
    return std::string{reinterpret_cast<const char *>(output.pbData), output.cbData};
}
#endif
}

std::optional<std::string> readCredential(const std::filesystem::path &path) {
    std::array<char, MAX_FILE> bytes{};
#if NINTTP_PLATFORM_WINDOWS == 1
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return std::nullopt;
        fileError();
    }
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD count{};
    const bool ok = GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr);
    CloseHandle(file);
    if (!ok || count == 0 || count == bytes.size())
        fileError();
    return protect(std::string{bytes.data(), count}, false);
#else
    const int file = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (file < 0) {
        if (errno == ENOENT)
            return std::nullopt;
        fileError();
    }
    struct stat info{};
    std::size_t size = 0;
    bool ok = fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
              (info.st_mode & 077) == 0;
    while (ok && size < bytes.size()) {
        const auto n = read(file, bytes.data() + size, bytes.size() - size);
        if (n == 0)
            break;
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            ok = false;
            break;
        }
        size += static_cast<std::size_t>(n);
    }
    close(file);
    if (!ok || size == 0 || size == bytes.size())
        fileError();
    return std::string{bytes.data(), size};
#endif
}

void saveCredential(const std::filesystem::path &path, const std::string &value) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
#if NINTTP_PLATFORM_WINDOWS == 1
    const auto bytes = protect(value, true);
    temporary += ".tmp." + std::to_string(GetCurrentProcessId());
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        fileError();
    DWORD count{};
    const bool written = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
                         count == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!written || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        fileError();
    }
#else
    temporary += ".tmp." + std::to_string(getpid());
    const int file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0)
        fileError();
    std::size_t size = 0;
    while (size < value.size()) {
        const auto n = write(file, value.data() + size, value.size() - size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        size += static_cast<std::size_t>(n);
    }
    const bool written = size == value.size() && fsync(file) == 0;
    close(file);
    // Publish only a complete file and leave any existing credential intact.
    const bool published = written && link(temporary.c_str(), path.c_str()) == 0;
    unlink(temporary.c_str());
    if (!published)
        fileError();
#endif
}

void removeCredential(const std::filesystem::path &path) {
    std::filesystem::remove(path);
}

} // namespace registry
