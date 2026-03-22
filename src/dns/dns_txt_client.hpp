#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace keen_pbr3 {

// Query a single TXT record from the configured DNS resolver.
// Returns the first TXT answer payload when available.
std::optional<std::string> query_dns_txt_record(const std::string& dns_server_address,
                                                const std::string& domain,
                                                std::chrono::milliseconds timeout,
                                                std::string* error_out = nullptr);

// Normalize TXT payload variants (quoted/value-wrapped) to a raw md5-like value.
std::string normalize_dns_txt_md5(const std::string& txt_payload);

} // namespace keen_pbr3
