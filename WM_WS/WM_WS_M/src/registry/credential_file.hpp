#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace registry {

std::optional<std::string> readCredential(const std::filesystem::path &path);
void saveCredential(const std::filesystem::path &path, const std::string &value);
void removeCredential(const std::filesystem::path &path);

} // namespace registry
