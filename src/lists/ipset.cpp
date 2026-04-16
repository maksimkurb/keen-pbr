#include "ipset.hpp"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace keen_pbr3 {

// --- IpTrie implementation ---

void IpTrie::insert(const uint8_t* bits, int prefix_len, int max_bits) {
    if (prefix_len < 0 || prefix_len > max_bits) {
        throw std::invalid_argument("invalid prefix length");
    }

    Node* node = &root_;
    for (int i = 0; i < prefix_len; ++i) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        int bit = (bits[byte_idx] >> bit_idx) & 1;

        if (!node->children[bit]) {
            node->children[bit] = std::make_unique<Node>();
        }
        node = node->children[bit].get();

        // If we hit an existing prefix that covers this one, no need to insert
        if (node->is_prefix) {
            return;
        }
    }
    node->is_prefix = true;
}

bool IpTrie::contains(const uint8_t* bits, int max_bits) const {
    const Node* node = &root_;

    if (node->is_prefix) {
        return true; // default route (prefix_len=0) matches everything
    }

    for (int i = 0; i < max_bits; ++i) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        int bit = (bits[byte_idx] >> bit_idx) & 1;

        if (!node->children[bit]) {
            return false;
        }
        node = node->children[bit].get();

        if (node->is_prefix) {
            return true;
        }
    }
    return false;
}

// --- IPv4 parsing ---

bool IpSet::parse_ipv4(std::string_view s, std::array<uint8_t, 4>& out) {
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            if (pos >= s.size() || s[pos] != '.') return false;
            ++pos;
        }
        size_t start = pos;
        // Find end of this octet
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        if (pos == start) return false;

        unsigned int val = 0;
        auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + pos, val);
        if (ec != std::errc{} || ptr != s.data() + pos) return false;
        if (val > 255) return false;
        out[i] = static_cast<uint8_t>(val);
    }
    return pos == s.size();
}

// --- IPv6 parsing ---

bool IpSet::parse_ipv6(std::string_view s, std::array<uint8_t, 16>& out) {
    out.fill(0);

    // Handle IPv4-mapped IPv6 (::ffff:1.2.3.4)
    auto v4_sep = s.rfind(':');
    if (v4_sep != std::string_view::npos && v4_sep + 1 < s.size()) {
        auto after_colon = s.substr(v4_sep + 1);
        if (after_colon.find('.') != std::string_view::npos) {
            // Has dots after last colon -> IPv4-mapped
            std::array<uint8_t, 4> v4{};
            if (!parse_ipv4(after_colon, v4)) return false;
            // Parse the IPv6 prefix part (before the v4 part)
            // Replace the v4 part with two synthetic groups
            // Build a new string: prefix + two hex groups for the v4 bytes
            std::string modified;
            modified.reserve(s.size());
            modified.append(s.data(), v4_sep + 1);
            // Convert v4 bytes to two 16-bit hex groups
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%x:%x",
                          (static_cast<unsigned>(v4[0]) << 8) | v4[1],
                          (static_cast<unsigned>(v4[2]) << 8) | v4[3]);
            modified.append(buf);
            return parse_ipv6(modified, out);
        }
    }

    // Count groups and find "::" position
    int double_colon_pos = -1; // group index where :: appears
    std::vector<uint16_t> groups;
    groups.reserve(8);

    size_t pos = 0;
    if (s.size() >= 2 && s[0] == ':' && s[1] == ':') {
        double_colon_pos = 0;
        pos = 2;
        if (pos == s.size()) {
            // Just "::" - all zeros
            return true;
        }
    }

    while (pos < s.size()) {
        // Check for "::" in middle/end
        if (pos < s.size() && s[pos] == ':') {
            if (pos + 1 < s.size() && s[pos + 1] == ':') {
                if (double_colon_pos >= 0) return false; // multiple :: not allowed
                double_colon_pos = static_cast<int>(groups.size());
                pos += 2;
                if (pos == s.size()) break;
                continue;
            }
            ++pos; // skip single ':'
        }

        // Parse hex group
        size_t start = pos;
        while (pos < s.size() && pos - start < 4 &&
               ((s[pos] >= '0' && s[pos] <= '9') ||
                (s[pos] >= 'a' && s[pos] <= 'f') ||
                (s[pos] >= 'A' && s[pos] <= 'F'))) {
            ++pos;
        }
        if (pos == start) return false;

        unsigned int val = 0;
        auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + pos, val, 16);
        if (ec != std::errc{} || ptr != s.data() + pos) return false;
        if (val > 0xFFFF) return false;
        groups.push_back(static_cast<uint16_t>(val));

        if (pos < s.size() && s[pos] != ':') return false;
    }

    // Expand :: and fill output
    if (double_colon_pos >= 0) {
        int total_groups = static_cast<int>(groups.size());
        int missing = 8 - total_groups;
        if (missing < 0) return false;

        // Build full 8-group list
        int out_idx = 0;
        for (int i = 0; i < double_colon_pos && i < total_groups; ++i) {
            const std::size_t byte_offset = static_cast<std::size_t>(out_idx) * 2U;
            out[byte_offset] = static_cast<uint8_t>(groups[i] >> 8);
            out[byte_offset + 1U] = static_cast<uint8_t>(groups[i] & 0xFF);
            ++out_idx;
        }
        out_idx += missing; // skip zero-filled groups
        for (int i = double_colon_pos; i < total_groups; ++i) {
            const std::size_t byte_offset = static_cast<std::size_t>(out_idx) * 2U;
            out[byte_offset] = static_cast<uint8_t>(groups[i] >> 8);
            out[byte_offset + 1U] = static_cast<uint8_t>(groups[i] & 0xFF);
            ++out_idx;
        }
    } else {
        if (groups.size() != 8) return false;
        for (std::size_t i = 0; i < 8U; ++i) {
            const std::size_t byte_offset = i * 2U;
            out[byte_offset] = static_cast<uint8_t>(groups[i] >> 8);
            out[byte_offset + 1U] = static_cast<uint8_t>(groups[i] & 0xFF);
        }
    }

    return true;
}

// --- IpSet public interface ---

void IpSet::add_address(const std::string& addr) {
    std::array<uint8_t, 4> v4{};
    if (parse_ipv4(addr, v4)) {
        v4_trie_.insert(v4.data(), 32, 32);
        return;
    }

    std::array<uint8_t, 16> v6{};
    if (parse_ipv6(addr, v6)) {
        v6_trie_.insert(v6.data(), 128, 128);
        return;
    }

    throw std::invalid_argument("invalid IP address: " + addr);
}

void IpSet::add_cidr(const std::string& cidr) {
    auto slash = cidr.find('/');
    if (slash == std::string::npos) {
        throw std::invalid_argument("invalid CIDR notation (no /): " + cidr);
    }

    auto ip_part = std::string_view(cidr).substr(0, slash);
    auto prefix_str = std::string_view(cidr).substr(slash + 1);

    unsigned int prefix_len = 0;
    auto [ptr, ec] = std::from_chars(prefix_str.data(), prefix_str.data() + prefix_str.size(), prefix_len);
    if (ec != std::errc{} || ptr != prefix_str.data() + prefix_str.size()) {
        throw std::invalid_argument("invalid CIDR prefix length: " + cidr);
    }

    std::array<uint8_t, 4> v4{};
    if (parse_ipv4(ip_part, v4)) {
        if (prefix_len > 32) {
            throw std::invalid_argument("IPv4 prefix length > 32: " + cidr);
        }
        v4_trie_.insert(v4.data(), static_cast<int>(prefix_len), 32);
        return;
    }

    std::array<uint8_t, 16> v6{};
    if (parse_ipv6(ip_part, v6)) {
        if (prefix_len > 128) {
            throw std::invalid_argument("IPv6 prefix length > 128: " + cidr);
        }
        v6_trie_.insert(v6.data(), static_cast<int>(prefix_len), 128);
        return;
    }

    throw std::invalid_argument("invalid CIDR notation: " + cidr);
}

bool IpSet::contains(const std::string& addr) const {
    std::array<uint8_t, 4> v4{};
    if (parse_ipv4(addr, v4)) {
        return v4_trie_.contains(v4.data(), 32);
    }

    std::array<uint8_t, 16> v6{};
    if (parse_ipv6(addr, v6)) {
        return v6_trie_.contains(v6.data(), 128);
    }

    throw std::invalid_argument("invalid IP address: " + addr);
}

} // namespace keen_pbr3
