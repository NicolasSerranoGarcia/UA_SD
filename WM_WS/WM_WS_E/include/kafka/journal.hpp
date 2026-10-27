#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace kafka {

// Alternating synced checkpoints retain the previous valid state after a torn write.
class Journal {
  public:
    explicit Journal(std::filesystem::path path);
    [[nodiscard]] std::optional<boost::json::object> load();
    void save(const boost::json::object &state);

  private:
    std::filesystem::path path_;
    std::uint64_t revision_ = 0;
};

} // namespace kafka
