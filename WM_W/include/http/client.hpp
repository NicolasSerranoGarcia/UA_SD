#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace wm_http {

struct Response {
    long status;
    std::string body;
};

class CurlSession {
public:
    CurlSession();
    ~CurlSession();
    CurlSession(const CurlSession &) = delete;
    CurlSession &operator=(const CurlSession &) = delete;
};

// With a JSON body this sends a PUT, otherwise a GET.
std::expected<Response, std::string> request(const std::string &url,
                                             std::optional<std::string_view> json = std::nullopt);

std::string endpoint(const std::string &base, std::string_view path);

} // namespace wm_http
