#pragma once

#include "../config/config.hpp"

#include <cstdint>
#include <string>

namespace keen_pbr3 {

struct URLTestResult {
    bool success{false};
    uint32_t latency_ms{0};
    std::string error;
};

class URLTester {
public:
    URLTester();
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
};

} // namespace keen_pbr3
