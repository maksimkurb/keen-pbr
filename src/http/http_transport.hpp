#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct HttpTransportRequest {
    std::string url;
    long timeout_ms{0};
    std::string user_agent;
    uint32_t fwmark{0};
    long max_redirects{5};
    std::vector<std::string> headers;
    bool discard_body{false};
    size_t max_response_size{size_t{8} * 1024U * 1024U};
};

struct HttpTransportResponse {
    long status_code{0};
    std::string body;
    // Lower-case names; values belong only to the final response after redirects.
    std::map<std::string, std::string> headers;
    std::chrono::milliseconds elapsed{0};
};

class HttpTransportError : public std::runtime_error {
public:
    explicit HttpTransportError(const std::string& message) : std::runtime_error(message) {}
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpTransportResponse perform(const HttpTransportRequest& request) = 0;
};

class LibcurlHttpTransport final : public HttpTransport {
public:
    HttpTransportResponse perform(const HttpTransportRequest& request) override;
};

std::shared_ptr<HttpTransport> default_http_transport();

} // namespace keen_pbr3
