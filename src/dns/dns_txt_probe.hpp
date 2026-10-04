#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

enum class DnsTxtProbeStatus {
    Ok,           // a TXT answer was received
    Missing,      // NXDOMAIN, or NOERROR without a TXT answer
    QueryFailed,  // timeout, refused, socket error, SERVFAIL, truncated, malformed
};

struct DnsTxtProbeResult {
    DnsTxtProbeStatus status{DnsTxtProbeStatus::QueryFailed};
    std::string txt;    // TXT character-strings of the first answer, concatenated
    std::string error;  // set for QueryFailed
};

// Builds a recursive IN TXT query for `name` (no TCP length prefix).
std::vector<std::uint8_t> build_dns_txt_query(std::uint16_t transaction_id,
                                              std::string_view name);

// Interprets a response to the query built by build_dns_txt_query(): verifies
// the transaction id and the question, rejects truncated/error responses.
DnsTxtProbeResult parse_dns_txt_response(const std::uint8_t* data,
                                         std::size_t size,
                                         std::uint16_t transaction_id,
                                         std::string_view name);

// Blocking UDP query straight to `address`:`port` (no resolv.conf, no res_*,
// so it is thread-safe and independent of the system resolver setup).
// Never throws.
DnsTxtProbeResult probe_dns_txt(const std::string& address,
                                std::uint16_t port,
                                std::string_view name,
                                std::chrono::milliseconds timeout);

} // namespace keen_pbr3
