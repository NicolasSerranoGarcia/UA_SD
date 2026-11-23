#include "http/client.hpp"

#include <curl/curl.h>
#include <memory>
#include <stdexcept>

namespace wm_http {
namespace {
constexpr std::size_t MAX_BODY = 128 * 1024;

std::size_t collect(char *data, std::size_t size, std::size_t count, void *context) noexcept {
    auto &body = *static_cast<std::string *>(context);
    const auto bytes = size * count;
    if (bytes > MAX_BODY - body.size())
        return 0;
    try {
        body.append(data, bytes);
        return bytes;
    } catch (...) {
        return 0;
    }
}
}

CurlSession::CurlSession() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("Could not initialize the HTTP client.");
}

CurlSession::~CurlSession() {
    curl_global_cleanup();
}

std::string endpoint(const std::string &base, std::string_view path) {
    auto url = base;
    while (url.ends_with('/'))
        url.pop_back();
    url.append(path);
    return url;
}

std::expected<Response, std::string> request(const std::string &url,
                                             std::optional<std::string_view> json) {
    Response response{};
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
        return std::unexpected("Could not initialize the HTTP request.");
    auto *curl = handle.get();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "HTTP,HTTPS");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "127.0.0.1,localhost");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "WM_W/2.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    if (json) {
        headers.reset(curl_slist_append(nullptr, "Content-Type: application/json"));
        if (!headers)
            return std::unexpected("Could not initialize the HTTP request headers.");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(json->size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json->data());
    }
    if (const auto result = curl_easy_perform(curl); result != CURLE_OK)
        return std::unexpected(curl_easy_strerror(result));
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

} // namespace wm_http
