#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

constexpr std::uint16_t kDnsTypeA = 1;
constexpr std::uint16_t kDnsTypeTXT = 16;
constexpr std::uint16_t kDnsTypeAAAA = 28;
constexpr std::uint16_t kDnsClassIN = 1;
constexpr std::uint16_t kDnsClassCH = 3;

enum class DnsQueryStatus { Ok, NoData, NxDomain, ServFail, Refused, OtherRcode, Timeout, NetworkError, Malformed };
// "ok","nodata","nxdomain","servfail","refused","rcode","timeout","network_error","malformed"
const char* dns_query_status_name(DnsQueryStatus s);

struct DnsQueryResult {
    DnsQueryStatus status{DnsQueryStatus::NetworkError};
    std::uint8_t rcode{0};
    bool via_tcp{false};
    std::vector<std::string> addresses;    // numeric, only records of the queried type
    std::vector<std::string> cname_chain;  // from dns_wire::ParsedResponse
    std::string error;
};

// Query message without TCP length prefix. RD=1. With edns0, appends an OPT RR
// (UDP payload 1232, no options) and sets ARCOUNT=1.
std::vector<std::uint8_t> build_dns_query(std::uint16_t id, std::string_view name,
                                          std::uint16_t qtype, std::uint16_t qclass, bool edns0);

// Blocking query to address:port only (never resolv.conf, never res_*). UDP
// first; on TC=1 retries the same question over TCP. Never throws.
DnsQueryResult dns_query(const std::string& address, std::uint16_t port, std::string_view name,
                         std::uint16_t qtype, std::uint16_t qclass, std::chrono::milliseconds timeout);

// True when anything DNS-shaped answered `version.bind CH TXT` (any rcode).
bool dns_probe_responding(const std::string& address, std::uint16_t port,
                          std::chrono::milliseconds timeout);

} // namespace keen_pbr3
