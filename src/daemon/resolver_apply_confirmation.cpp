#include "resolver_apply_confirmation.hpp"

#include <thread>

namespace keen_pbr3 {

bool resolver_hash_confirmation_matches(const ResolverConfigHashProbeResult& result,
                                        const std::string& expected_hash,
                                        std::int64_t apply_started_ts) {
    return result.status == ResolverConfigHashProbeStatus::SUCCESS &&
           result.parsed_value.hash == expected_hash && result.parsed_value.ts.has_value() &&
           *result.parsed_value.ts >= apply_started_ts;
}

bool wait_for_resolver_hash_confirmation(
    const std::string& expected_hash,
    std::int64_t apply_started_ts,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds interval,
    const std::function<ResolverConfigHashProbeResult()>& probe,
    std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    ResolverConfigHashProbeResult last_result;
    do {
        last_result = probe();
        if (resolver_hash_confirmation_matches(last_result, expected_hash, apply_started_ts)) {
            return true;
        }
        std::this_thread::sleep_for(interval);
    } while (std::chrono::steady_clock::now() < deadline);

    error = "resolver config hash confirmation timed out";
    if (last_result.status == ResolverConfigHashProbeStatus::SUCCESS) {
        error += " (hash/timestamp did not match candidate)";
    } else if (!last_result.error.empty()) {
        error += ": " + last_result.error;
    }
    return false;
}

} // namespace keen_pbr3
