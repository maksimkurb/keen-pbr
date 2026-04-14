#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace keen_pbr3 {

struct ResolverConfigHashTxtValue {
    std::optional<std::int64_t> ts;
    std::string hash;
};

enum class ResolverConfigHashProbeStatus : uint8_t {
    SUCCESS,
    NO_USABLE_TXT,
    INVALID_TXT,
    QUERY_FAILED,
};

struct ResolverConfigHashProbeResult {
    ResolverConfigHashProbeStatus status{ResolverConfigHashProbeStatus::QUERY_FAILED};
    std::optional<std::string> raw_txt;
    ResolverConfigHashTxtValue parsed_value;
    std::string error;
};

// Query a single TXT record from the configured DNS resolver.
// Returns the selected TXT answer payload when available.
std::optional<std::string> query_dns_txt_record(const std::string& dns_server_address,
                                                const std::string& domain,
                                                std::chrono::milliseconds timeout,
                                                std::string* error_out = nullptr);

// Normalize TXT payload variants (quoted/value-wrapped) to a raw md5-like value.
std::string normalize_dns_txt_md5(const std::string& txt_payload);
// Parse TXT payload variants like "<ts>|<hash>" and return timestamp/hash parts.
ResolverConfigHashTxtValue parse_resolver_config_hash_txt(const std::string& txt_payload);
// Validate whether the parsed payload contains a usable md5 hash.
bool is_valid_resolver_config_hash_txt_value(const ResolverConfigHashTxtValue& value);
// Query and classify the resolver config-hash TXT payload in one step.
ResolverConfigHashProbeResult query_resolver_config_hash_txt(const std::string& dns_server_address,
                                                            const std::string& domain,
                                                            std::chrono::milliseconds timeout);

} // namespace keen_pbr3
