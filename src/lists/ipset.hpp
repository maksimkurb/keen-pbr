#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// Binary trie for efficient IP prefix matching.
// Stores prefixes as bit strings; contains() walks the trie
// and returns true if any stored prefix is a prefix of the query.
class IpTrie {
public:
    void insert(const uint8_t* bits, int prefix_len, int max_bits);
    bool contains(const uint8_t* bits, int max_bits) const;

private:
    struct Node {
        std::unique_ptr<Node> children[2];
        bool is_prefix = false; // true if a stored prefix ends here
    };

    Node root_;
};

// Efficient IP set supporting individual addresses and CIDR subnets
// for both IPv4 and IPv6. Uses a binary trie for O(W) lookup
// where W is the address width (32 for IPv4, 128 for IPv6).
class IpSet {
public:
    // Add an individual IPv4 or IPv6 address (e.g. "192.168.1.1", "::1")
    void add_address(const std::string& addr);

    // Add a CIDR subnet (e.g. "10.0.0.0/8", "2001:db8::/32")
    void add_cidr(const std::string& cidr);

    // Check if an IP address matches any stored address or falls within any stored subnet
    bool contains(const std::string& addr) const;

private:
    // Parse an IPv4 address string into 4 bytes. Returns false on failure.
    static bool parse_ipv4(std::string_view s, std::array<uint8_t, 4>& out);

    // Parse an IPv6 address string into 16 bytes. Returns false on failure.
    static bool parse_ipv6(std::string_view s, std::array<uint8_t, 16>& out);

    IpTrie v4_trie_;
    IpTrie v6_trie_;
};

} // namespace keen_pbr3
