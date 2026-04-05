#include "url_tester.hpp"

#include "../log/logger.hpp"

#include <chrono>
#include <cstdint>
#include <curl/curl.h>
#include <sys/socket.h>
#include <thread>

namespace keen_pbr3 {

// Discard response body
static size_t discard_callback(char* /*ptr*/, size_t size, size_t nmemb,
                               void* /*userdata*/) {
    if (nmemb != 0 && size > SIZE_MAX / nmemb) return 0;
    return size * nmemb;
}

// Socket option callback to set SO_MARK on the socket for policy routing
static int sockopt_callback(void* clientp, curl_socket_t curlfd,
                            curlsocktype /*purpose*/) {
    auto mark = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clientp));
    if (setsockopt(curlfd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark)) < 0) {
        return CURL_SOCKOPT_ERROR;
    }
    return CURL_SOCKOPT_OK;
}

URLTester::URLTester() {}

URLTester::~URLTester() = default;

URLTestResult URLTester::test_once(const std::string& url, uint32_t fwmark,
                                   uint32_t timeout_ms) {
    URLTestResult result;
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("url_test_once_start",
                             "url={} fwmark={} timeout_ms={}",
                             url,
                             fwmark,
                             timeout_ms);

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize curl handle";
        Logger::instance().trace("url_test_once_error",
                                 "url={} fwmark={} duration_ms={} error={}",
                                 url,
                                 fwmark,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 result.error);
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "keen-pbr-urltest");

    // Route test traffic through the outbound's fwmark via SO_MARK
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_callback);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA,
                     reinterpret_cast<void*>(static_cast<uintptr_t>(fwmark)));

    auto start = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto end = std::chrono::steady_clock::now();

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        Logger::instance().trace("url_test_once_error",
                                 "url={} fwmark={} duration_ms={} error={}",
                                 url,
                                 fwmark,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     end - started_at).count(),
                                 result.error);
        return result;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code >= 200 && http_code < 300) {
        result.success = true;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        result.latency_ms = static_cast<uint32_t>(ms.count());
        Logger::instance().trace("url_test_once_end",
                                 "url={} fwmark={} duration_ms={} latency_ms={} http_code={}",
                                 url,
                                 fwmark,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     end - started_at).count(),
                                 result.latency_ms,
                                 http_code);
    } else {
        result.error = "HTTP " + std::to_string(http_code);
        Logger::instance().trace("url_test_once_error",
                                 "url={} fwmark={} duration_ms={} error={}",
                                 url,
                                 fwmark,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     end - started_at).count(),
                                 result.error);
    }

    return result;
}

URLTestResult URLTester::test(const std::string& url, uint32_t fwmark,
                              uint32_t timeout_ms, const RetryConfig& retry) {
    URLTestResult best;
    best.error = "No attempts made";
    const auto total_started_at = std::chrono::steady_clock::now();
    const auto attempts = static_cast<uint32_t>(retry.attempts.value_or(1));
    Logger::instance().trace("url_test_start",
                             "url={} fwmark={} timeout_ms={} attempts={}",
                             url,
                             fwmark,
                             timeout_ms,
                             attempts);

    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(retry.interval_ms.value_or(1000)));
        }

        Logger::instance().trace("url_test_attempt_start",
                                 "url={} fwmark={} attempt={} attempts={}",
                                 url,
                                 fwmark,
                                 attempt + 1,
                                 attempts);
        auto result = test_once(url, fwmark, timeout_ms);
        if (result.success) {
            if (!best.success || result.latency_ms < best.latency_ms) {
                best = result;
            }
            Logger::instance().trace("url_test_end",
                                     "url={} fwmark={} duration_ms={} success=true latency_ms={}",
                                     url,
                                     fwmark,
                                     std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - total_started_at).count(),
                                     best.latency_ms);
            return best;
        }

        best.error = result.error;
        Logger::instance().trace("url_test_attempt_end",
                                 "url={} fwmark={} attempt={} attempts={} success=false error={}",
                                 url,
                                 fwmark,
                                 attempt + 1,
                                 attempts,
                                 best.error);
    }

    Logger::instance().trace("url_test_end",
                             "url={} fwmark={} duration_ms={} success=false error={}",
                             url,
                             fwmark,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - total_started_at).count(),
                             best.error);
    return best;
}

} // namespace keen_pbr3
