#pragma once

#include "../util/byte_view.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3::dns_wire {

struct PacketLayout {
    uint8_t ip_version{0};         // 4 or 6
    uint8_t l4_proto{0};           // IPPROTO_UDP or IPPROTO_TCP
    std::size_t l3_header_len{0};  // IPv4: IHL*4; IPv6: 40 + skipped extension headers
    std::size_t l4_header_len{0};  // UDP 8; TCP data offset*4
    std::size_t payload_offset{0}; // l3 + l4
    std::size_t payload_len{0};
};

// IPv4 (fragments rejected) and IPv6 (Hop-by-Hop/Routing/DestOpts skipped, Fragment rejected).
std::optional<PacketLayout> parse_packet_layout(ByteView l3_packet);

struct AddressRecord {
    uint8_t family{4}; // 4 or 6
    std::array<uint8_t, 16> addr{};
    uint32_t ttl{0};
};

struct ParsedResponse {
    uint16_t id{0};
    uint16_t flags{0};
    uint8_t rcode{0};
    bool truncated{false};
    uint16_t qtype{0};
    uint16_t qclass{0};
    std::string qname;                    // lowercase, no trailing dot
    std::size_t question_end{0};          // offset just past QTYPE/QCLASS in the message
    std::vector<std::string> cname_chain; // owners and targets of answer CNAMEs, deduplicated
    std::vector<AddressRecord> addresses; // A/AAAA (IN) from the answer section

    void clear();
};

// Parses a DNS response message (no TCP length prefix). Requires QR=1, QDCOUNT==1.
bool parse_response(ByteView dns_message, ParsedResponse& out);

// Parses only the question portion of a DNS response (header + question section).
// Returns true if successful; extracts qname, qtype, and rcode without parsing answers.
// Useful for quick early-exit decisions like IPv6-disabled AAAA checks.
bool parse_question_only(ByteView dns_message, ParsedResponse& out);

// TCP payload begins with a 2-byte length; returns the message only if fully contained.
std::optional<ByteView> tcp_single_message(ByteView tcp_payload);

bool is_marker_name(std::string_view qname, std::string_view marker);

// Checks if an address is learnable (not 0.0.0.0, ::, 127.0.0.0/8, or ::1).
// `addr` is 4 bytes for IPv4 (family=4) or 16 bytes for IPv6 (family=6).
bool is_learnable_address(uint8_t family, const uint8_t* addr);

// Builds a replacement L3 packet (UDP only; IPv6 without extension headers only).
// The question is copied byte-for-byte so 0x20 case randomization still matches.
std::optional<std::vector<uint8_t>> build_marker_packet(ByteView original_l3_packet,
                                                        const PacketLayout& layout,
                                                        const ParsedResponse& resp,
                                                        const std::array<uint8_t, 4>& answer_ipv4);

uint16_t inet_checksum(const uint8_t* data, std::size_t len, uint32_t initial_sum = 0);

} // namespace keen_pbr3::dns_wire
