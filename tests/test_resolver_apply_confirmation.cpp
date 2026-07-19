#include <doctest/doctest.h>

#include "../src/daemon/resolver_apply_confirmation.hpp"

using namespace keen_pbr3;

namespace {

ResolverConfigHashProbeResult successful_probe(std::string hash, std::int64_t timestamp) {
    ResolverConfigHashProbeResult result;
    result.status = ResolverConfigHashProbeStatus::SUCCESS;
    result.parsed_value.hash = std::move(hash);
    result.parsed_value.ts = timestamp;
    return result;
}

} // namespace

TEST_CASE("resolver apply confirmation accepts only matching fresh TXT") {
    CHECK(resolver_hash_confirmation_matches(successful_probe("abcd", 100), "abcd", 100));
    CHECK(resolver_hash_confirmation_matches(successful_probe("abcd", 101), "abcd", 100));
    CHECK_FALSE(resolver_hash_confirmation_matches(successful_probe("abcd", 99), "abcd", 100));
    CHECK_FALSE(resolver_hash_confirmation_matches(successful_probe("other", 100), "abcd", 100));
}

TEST_CASE("resolver apply confirmation polls until a fresh candidate TXT appears") {
    int calls = 0;
    std::string error;
    CHECK(wait_for_resolver_hash_confirmation(
        "candidate", 100, std::chrono::milliseconds{20}, std::chrono::milliseconds{0},
        [&calls] {
            ++calls;
            return calls == 1 ? successful_probe("candidate", 99)
                              : successful_probe("candidate", 100);
        }, error));
    CHECK(calls >= 2);
    CHECK(error.empty());
}

TEST_CASE("resolver apply confirmation returns a diagnostic on timeout") {
    std::string error;
    CHECK_FALSE(wait_for_resolver_hash_confirmation(
        "candidate", 100, std::chrono::milliseconds{0}, std::chrono::milliseconds{0},
        [] { return successful_probe("candidate", 99); }, error));
    CHECK(error.find("timed out") != std::string::npos);
}
