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

// Query a single TXT record from the configured DNS resolver.
// Returns the first TXT answer payload when available.
std::optional<std::string> query_dns_txt_record(const std::string& dns_server_address,
                                                const std::string& domain,
                                                std::chrono::milliseconds timeout,
                                                std::string* error_out = nullptr);

// Normalize TXT payload variants (quoted/value-wrapped) to a raw md5-like value.
std::string normalize_dns_txt_md5(const std::string& txt_payload);
// Parse TXT payload variants like "<ts>|<hash>" and return timestamp/hash parts.
ResolverConfigHashTxtValue parse_resolver_config_hash_txt(const std::string& txt_payload);

} // namespace keen_pbr3
