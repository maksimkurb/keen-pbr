#include "http_client.hpp"

#include "../log/logger.hpp"

#include <cctype>

namespace keen_pbr3 {
HttpError::HttpError(const std::string& message, long status_code)
    : std::runtime_error(message), status_code_(status_code) {}
long HttpError::status_code() const noexcept { return status_code_; }

namespace {
std::string trim_header_value(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
HttpTransportRequest request_for(const std::string& url, std::chrono::seconds timeout,
                                 const std::string& user_agent, size_t max_size,
                                 uint32_t fwmark) {
    HttpTransportRequest request;
    request.url = url;
    request.timeout_ms = static_cast<long>(timeout.count() * 1000);
    request.user_agent = user_agent;
    request.fwmark = fwmark;
    request.max_redirects = 5;
    request.max_response_size = max_size;
    return request;
}
void throw_for_status(long status) {
    if (status >= 400) throw HttpError("HTTP error " + std::to_string(status), status);
}
} // namespace

void detail::capture_response_header_line(std::string_view header_view, std::string& etag,
                                          std::string& last_modified) {
    const std::string header(header_view);
    if (header.rfind("HTTP/", 0) == 0 || header.rfind("http/", 0) == 0) {
        etag.clear(); last_modified.clear(); return;
    }
    const auto colon = header.find(':');
    if (colon == std::string::npos) return;
    std::string name = header.substr(0, colon);
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "etag") etag = trim_header_value(header.substr(colon + 1));
    if (name == "last-modified") last_modified = trim_header_value(header.substr(colon + 1));
}

HttpClient::HttpClient() : HttpClient(default_http_transport()) {}
HttpClient::HttpClient(std::shared_ptr<HttpTransport> transport) : transport_(std::move(transport)) {
    if (!transport_) throw std::invalid_argument("HttpClient requires an HTTP transport");
}
HttpClient::~HttpClient() = default;
void HttpClient::set_timeout(std::chrono::seconds timeout) { timeout_ = timeout; }
void HttpClient::set_user_agent(const std::string& user_agent) { user_agent_ = user_agent; }
void HttpClient::set_max_response_size(size_t bytes) { max_response_size_ = bytes; }

std::string HttpClient::download(const std::string& url, const HttpRequestOptions& options) {
    try {
        auto response = transport_->perform(request_for(url, timeout_, user_agent_, max_response_size_, options.fwmark));
        throw_for_status(response.status_code);
        return response.body;
    } catch (const HttpTransportError& error) {
        throw HttpError(error.what());
    }
}

ConditionalDownloadResult HttpClient::download_conditional(
    const std::string& url, const std::string& if_none_match, const std::string& if_modified_since,
    const HttpRequestOptions& options) {
    auto request = request_for(url, timeout_, user_agent_, max_response_size_, options.fwmark);
    if (!if_none_match.empty()) request.headers.push_back("If-None-Match: " + if_none_match);
    if (!if_modified_since.empty()) request.headers.push_back("If-Modified-Since: " + if_modified_since);
    try {
        const auto response = transport_->perform(request);
        ConditionalDownloadResult result;
        result.not_modified = response.status_code == 304;
        if (!result.not_modified) throw_for_status(response.status_code);
        if (!result.not_modified) result.body = response.body;
        const auto etag = response.headers.find("etag");
        if (etag != response.headers.end()) result.etag = etag->second;
        const auto modified = response.headers.find("last-modified");
        if (modified != response.headers.end()) result.last_modified = modified->second;
        return result;
    } catch (const HttpTransportError& error) {
        throw HttpError(error.what());
    }
}
} // namespace keen_pbr3
