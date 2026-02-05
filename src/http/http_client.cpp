#include "http_client.hpp"

#include <curl/curl.h>

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

} // namespace keen_pbr3
