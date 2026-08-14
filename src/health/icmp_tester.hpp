#pragma once

#include "url_tester.hpp"

#include <cstdint>
#include <sys/socket.h>
#include <string>

namespace keen_pbr3 {

// Linux ICMP echo probe bound to an outbound by SO_MARK.  A run succeeds when
// no more than max_failed packets time out, fail, or exceed max_rtt_ms.
class IcmpTester {
public:
    URLTestResult test(const std::string& target, uint32_t fwmark,
                       uint32_t count, uint32_t max_failed,
                       uint32_t packet_interval_ms, uint32_t timeout_ms,
                       uint32_t max_rtt_ms) const;
};

namespace icmp_detail {

bool reply_matches(int family, const unsigned char* data, size_t size,
                   const sockaddr_storage& source,
                   const sockaddr_storage& expected_source,
                   uint16_t identifier, uint16_t sequence);

// Raw IPv4 ICMP sockets prepend the IPv4 header to received packets. IPv6 raw
// ICMP sockets deliver the ICMPv6 header directly.
bool raw_reply_matches(int family, const unsigned char* data, size_t size,
                       const sockaddr_storage& source,
                       const sockaddr_storage& expected_source,
                       uint16_t identifier, uint16_t sequence);

} // namespace icmp_detail

} // namespace keen_pbr3
