#include "url_tester.hpp"

#include "../log/logger.hpp"

#include <chrono>
#include <thread>

namespace keen_pbr3 {
URLTester::URLTester() : URLTester(default_http_transport()) {}
URLTester::URLTester(std::shared_ptr<HttpTransport> transport) : transport_(std::move(transport)) {
    if (!transport_) throw std::invalid_argument("URLTester requires an HTTP transport");
}
URLTester::~URLTester() = default;

URLTestResult URLTester::test_once(const std::string& url, uint32_t fwmark, uint32_t timeout_ms) {
    URLTestResult result;
    HttpTransportRequest request;
    request.url = url;
    request.timeout_ms = static_cast<long>(timeout_ms);
    request.user_agent = "keen-pbr-urltest";
    request.fwmark = fwmark;
    request.max_redirects = 3;
    request.discard_body = true;
    try {
        const auto response = transport_->perform(request);
        if (response.status_code >= 200 && response.status_code < 300) {
            result.success = true;
            result.latency_ms = static_cast<uint32_t>(response.elapsed.count());
        } else {
            result.error = "HTTP " + std::to_string(response.status_code);
        }
    } catch (const HttpTransportError& error) {
        result.error = error.what();
    }
    return result;
}

URLTestResult URLTester::test(const std::string& url, uint32_t fwmark, uint32_t timeout_ms,
                              const RetryConfig& retry) {
    URLTestResult best;
    best.error = "No attempts made";
    const auto attempts = static_cast<uint32_t>(retry.attempts.value_or(1));
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(retry.interval_ms.value_or(1000)));
        auto result = test_once(url, fwmark, timeout_ms);
        if (result.success) return result;
        best.error = result.error;
    }
    return best;
}
} // namespace keen_pbr3
