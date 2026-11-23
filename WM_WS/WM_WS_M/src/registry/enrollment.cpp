#include "registry/enrollment.hpp"

#include "app/options.hpp"
#include "credential_file.hpp"
#include <algorithm>
#include <boost/json.hpp>
#include <curl/curl.h>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace registry {
namespace {
constexpr std::size_t MAX_BODY = 4096;

bool validSecret(std::string_view secret) {
    return secret.size() == 43 && std::ranges::all_of(secret, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

struct Response {
    long status{};
    std::string body;
};

struct CurlSession {
    CurlSession() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Cannot initialize the Registry HTTPS client.");
    }
    ~CurlSession() { curl_global_cleanup(); }
};

std::size_t collect(char *data, std::size_t size, std::size_t count, void *context) noexcept {
    auto &body = *static_cast<std::string *>(context);
    const auto length = size * count;
    if (length > MAX_BODY - body.size())
        return 0;
    try {
        body.append(data, length);
        return length;
    } catch (...) {
        return 0;
    }
}

Response request(const std::string &url, const std::string &caFile, const char *method,
                 std::string_view token, const std::string &body = {}) {
    static const CurlSession session;
    Response response;
    const auto authorization = "Authorization: Bearer " + std::string{token};
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        token.empty() ? nullptr : curl_slist_append(nullptr, authorization.c_str()), curl_slist_free_all);
    const auto *failure = "Registry request could not be initialized.";
    if (!token.empty() && !headers)
        throw std::runtime_error(failure);
    if (!body.empty()) {
        auto *extended = curl_slist_append(headers.get(), "Content-Type: application/json");
        if (!extended)
            throw std::runtime_error(failure);
        (void)headers.release();
        headers.reset(extended);
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
        throw std::runtime_error(failure);
    auto *curl = handle.get();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (!caFile.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, caFile.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 4000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
    if (const auto result = curl_easy_perform(curl); result != CURLE_OK)
        throw std::runtime_error(std::string{"Registry HTTPS request failed: "} + curl_easy_strerror(result));
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

boost::json::object object(const std::string &body) {
    boost::system::error_code error;
    auto json = boost::json::parse(body, error);
    if (error || !json.is_object())
        throw std::runtime_error("Registry returned invalid enrollment data.");
    return std::move(json.as_object());
}

std::string field(const boost::json::object &body, std::string_view name) {
    const auto *value = body.if_contains(name);
    if (!value || !value->is_string())
        throw std::runtime_error("Registry enrollment data is incomplete.");
    const auto &str = value->as_string();
    return {str.data(), str.size()};
}

void verifyId(const boost::json::object &body, std::int64_t expected) {
    const auto *id = body.if_contains("station_id");
    if (!id || !id->is_int64() || id->as_int64() != expected)
        throw std::runtime_error("Registry enrollment data is for a different station.");
}
}

Enrollment::Enrollment(const app::Options &options)
    : origin_(options.registryUrl), url_(origin_ + "/api/registry/stations/" + options.wsId),
      city_(options.location), caFile_(options.registryCaFile), stationId_(std::stoll(options.wsId)),
      path_(std::filesystem::path{options.registryStateDir} / (options.wsId + ".credential")) {}

std::string Enrollment::perform(Action action) const {
    try {
        const auto saved = readCredential(path_);
        std::string credential;
        if (saved) {
            const auto data = object(*saved);
            verifyId(data, stationId_);
            credential = field(data, "credential");
            if (field(data, "registry") != origin_ || !validSecret(credential))
                return "Saved credential is for a different Registry or is invalid. Check the state directory.";
        }

        if (action == Action::Register) {
            if (saved)
                return "A credential is already saved. Refresh status or deregister before enrolling.";
            const auto body = boost::json::serialize(boost::json::object{{"city", city_}});
            const auto response = request(url_, caFile_, "PUT", {}, body);
            if (response.status == 409)
                return "This station ID is already registered. If you lost its credential, ask the Registry administrator to reset the enrollment.";
            if (response.status != 201)
                return "Registration failed (HTTP " + std::to_string(response.status) + ").";
            try {
                const auto enrollment = object(response.body);
                verifyId(enrollment, stationId_);
                credential = field(enrollment, "credential");
                if (!validSecret(credential) || field(enrollment, "city") != city_)
                    throw std::runtime_error("Invalid enrollment details.");
                saveCredential(path_, boost::json::serialize(boost::json::object{
                    {"station_id", stationId_}, {"registry", origin_}, {"credential", credential}}));
            } catch (const std::exception &) {
                return "Registry accepted registration, but Monitor could not save a valid credential. Ask the Registry administrator to reset the enrollment.";
            }
            return "Registered. Awaiting secure Central activation.";
        }

        if (!saved)
            return "Not registered. Choose Register to enroll this station.";
        const bool revoke = action == Action::Deregister;
        const auto response = request(url_, caFile_, revoke ? "DELETE" : "GET", credential);
        if (response.status == 401) {
            removeCredential(path_);
            return "Registry no longer accepts the saved credential. Choose Register to enroll again.";
        }
        if (revoke && response.status == 204) {
            removeCredential(path_);
            return "Deregistered. The local credential was removed.";
        }
        if (!revoke && response.status == 200) {
            const auto enrollment = object(response.body);
            verifyId(enrollment, stationId_);
            if (field(enrollment, "city") != city_)
                return "Registered for a different city. Deregister before changing the station location.";
            return "Registered. Awaiting secure Central activation.";
        }
        return "Registry returned HTTP " + std::to_string(response.status) + ". The saved credential was kept.";
    } catch (const std::exception &error) {
        return error.what();
    }
}

} // namespace registry
