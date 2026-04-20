#pragma once

#include <keen-pbr/version.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

struct HttpRequestOptions {
    uint32_t fwmark{0};
};

class HttpError : public std::runtime_error {
public:
    HttpError(const std::string& message, long status_code = 0);
    long status_code() const noexcept;

private:
    long status_code_;
};

struct ConditionalDownloadResult {
    bool not_modified = false;
    std::string body;
    std::string etag;
    std::string last_modified;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void set_timeout(std::chrono::seconds timeout);
    void set_user_agent(const std::string& user_agent);
    void set_max_response_size(size_t bytes); // default: 8 MiB

    std::string download(const std::string& url,
                         const HttpRequestOptions& options = {});

    ConditionalDownloadResult download_conditional(
        const std::string& url,
        const std::string& if_none_match = "",
        const std::string& if_modified_since = "",
        const HttpRequestOptions& options = {});

private:
    std::chrono::seconds timeout_{10};
    std::string user_agent_{"keen-pbr/" KEEN_PBR3_VERSION_STRING};
    size_t max_response_size_{size_t{8} * 1024U * 1024U}; // 8 MiB
};

} // namespace keen_pbr3
