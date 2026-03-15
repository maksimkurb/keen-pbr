#include "http_client.hpp"

#include <algorithm>
#include <curl/curl.h>
#include <sys/socket.h>

namespace keen_pbr3 {

// HttpError

HttpError::HttpError(const std::string& message, long status_code)
    : std::runtime_error(message), status_code_(status_code) {}

long HttpError::status_code() const noexcept { return status_code_; }

// write callback for libcurl
static size_t write_callback(char* ptr, size_t size, size_t nmemb,
                             void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    body->append(ptr, total);
    return total;
}

// header callback for capturing response headers
struct HeaderCapture {
    std::string etag;
    std::string last_modified;
};

static std::string trim_header_value(const std::string& s) {
    size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' ||
            s[start] == '\n'))
        ++start;
    size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' ||
            s[end - 1] == '\n'))
        --end;
    return s.substr(start, end - start);
}

static size_t header_callback(char* buffer, size_t size, size_t nitems,
                              void* userdata) {
    size_t total = size * nitems;
    auto* capture = static_cast<HeaderCapture*>(userdata);
    std::string header(buffer, total);

    // Case-insensitive prefix check
    auto starts_with_ci = [&](const std::string& prefix) {
        if (header.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(header[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        }
        return true;
    };

    if (starts_with_ci("etag:")) {
        capture->etag = trim_header_value(header.substr(5));
    } else if (starts_with_ci("last-modified:")) {
        capture->last_modified = trim_header_value(header.substr(14));
    }

    return total;
}

// HttpClient

HttpClient::HttpClient() {
    static bool curl_initialized = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)curl_initialized;
}

HttpClient::~HttpClient() = default;

void HttpClient::set_timeout(std::chrono::seconds timeout) {
    timeout_ = timeout;
}

void HttpClient::set_user_agent(const std::string& user_agent) {
    user_agent_ = user_agent;
}

void HttpClient::set_fwmark(uint32_t mark) {
    fwmark_ = mark;
}

static int sockopt_cb(void* userdata, curl_socket_t fd, curlsocktype) {
    uint32_t mark = *static_cast<uint32_t*>(userdata);
    if (mark != 0) {
        setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
    }
    return CURL_SOCKOPT_OK;
}

std::string HttpClient::download(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw HttpError("Failed to initialize curl handle");
    }

    std::string body;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_.count()));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_cb);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, &fwmark_);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw HttpError("HTTP request failed: " + err);
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code >= 400) {
        throw HttpError("HTTP error " + std::to_string(http_code), http_code);
    }

    return body;
}

ConditionalDownloadResult HttpClient::download_conditional(
    const std::string& url, const std::string& if_none_match,
    const std::string& if_modified_since) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw HttpError("Failed to initialize curl handle");
    }

    ConditionalDownloadResult result;
    HeaderCapture headers;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                     static_cast<long>(timeout_.count()));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_cb);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, &fwmark_);

    // Set conditional request headers
    struct curl_slist* req_headers = nullptr;
    if (!if_none_match.empty()) {
        std::string h = "If-None-Match: " + if_none_match;
        req_headers = curl_slist_append(req_headers, h.c_str());
    }
    if (!if_modified_since.empty()) {
        std::string h = "If-Modified-Since: " + if_modified_since;
        req_headers = curl_slist_append(req_headers, h.c_str());
    }
    if (req_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if (req_headers) {
        curl_slist_free_all(req_headers);
    }
    if (res != CURLE_OK) {
        std::string err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw HttpError("HTTP request failed: " + err);
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code == 304) {
        result.not_modified = true;
        result.body.clear();
        result.etag = headers.etag;
        result.last_modified = headers.last_modified;
        return result;
    }

    if (http_code >= 400) {
        throw HttpError("HTTP error " + std::to_string(http_code), http_code);
    }

    result.not_modified = false;
    result.etag = headers.etag;
    result.last_modified = headers.last_modified;
    return result;
}

} // namespace keen_pbr3
