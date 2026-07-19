#pragma once

#include "../dns/dns_txt_client.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace keen_pbr3 {

bool resolver_hash_confirmation_matches(const ResolverConfigHashProbeResult& result,
                                        const std::string& expected_hash,
                                        std::int64_t apply_started_ts);

bool wait_for_resolver_hash_confirmation(
    const std::string& expected_hash,
    std::int64_t apply_started_ts,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds interval,
    const std::function<ResolverConfigHashProbeResult()>& probe,
    std::string& error);

} // namespace keen_pbr3
