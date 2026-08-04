#pragma once

#include "../config/config.hpp"
#include "../http/http_transport.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace keen_pbr3 {

// Keep urltest probe timeouts independent from circuit-breaker cooldowns.
constexpr uint32_t kDefaultUrltestProbeTimeoutMs = 5000;

struct URLTestResult {
    bool success{false};
    uint32_t latency_ms{0};
    std::string error;
    std::optional<std::string> probe_target;
    std::optional<uint32_t> packets_attempted;
    std::optional<uint32_t> packets_sent;
    std::optional<uint32_t> packets_received;
    std::optional<uint32_t> packets_failed;
};

class URLTester {
public:
    URLTester();
    explicit URLTester(std::shared_ptr<HttpTransport> transport);
    ~URLTester();

    URLTester(const URLTester&) = delete;
    URLTester& operator=(const URLTester&) = delete;

    // Test a URL through an outbound identified by its fwmark.
    // Uses CURLOPT_MARK to route test traffic via the correct routing table.
    // Retries up to retry.attempts times with retry.interval_ms delay between attempts.
    // Returns the result with latency_ms from the fastest successful attempt.
    URLTestResult test(const std::string& url, uint32_t fwmark,
                       uint32_t timeout_ms, const RetryConfig& retry);

private:
    URLTestResult test_once(const std::string& url, uint32_t fwmark,
                            uint32_t timeout_ms);
    std::shared_ptr<HttpTransport> transport_;
};

} // namespace keen_pbr3
