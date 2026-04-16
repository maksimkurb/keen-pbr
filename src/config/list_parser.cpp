#include "list_parser.hpp"

#include <arpa/inet.h>

#include <charconv>

namespace keen_pbr3 {

static std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}

bool ListParser::is_ipv4(std::string_view s) {
    int octets = 0;
    size_t pos = 0;
    while (pos <= s.size() && octets < 4) {
        auto dot = s.find('.', pos);
        if (dot == std::string_view::npos) dot = s.size();
        auto part = s.substr(pos, dot - pos);
        if (part.empty() || part.size() > 3) return false;
        int val = 0;
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), val);
        if (ec != std::errc{} || ptr != part.data() + part.size()) return false;
        if (val < 0 || val > 255) return false;
        ++octets;
        pos = dot + 1;
    }
    return octets == 4 && pos == s.size() + 1;
}

bool ListParser::is_ipv6(std::string_view s) {
    if (s.empty()) return false;
    // Reject CIDR suffixes and scoped zone IDs. This parser classifies plain IPs;
    // CIDR is handled separately and zone IDs are not supported downstream.
    if (s.find('/') != std::string_view::npos) return false;
    if (s.find('%') != std::string_view::npos) return false;

    std::string ip(s);
    in6_addr parsed{};
    return inet_pton(AF_INET6, ip.c_str(), &parsed) == 1;
}

bool ListParser::is_cidr_v4(std::string_view s) {
    auto slash = s.find('/');
    if (slash == std::string_view::npos) return false;
    auto ip_part = s.substr(0, slash);
    auto prefix_part = s.substr(slash + 1);
    if (!is_ipv4(ip_part)) return false;
    if (prefix_part.empty() || prefix_part.size() > 2) return false;
    int prefix = 0;
    auto [ptr, ec] = std::from_chars(prefix_part.data(), prefix_part.data() + prefix_part.size(), prefix);
    if (ec != std::errc{} || ptr != prefix_part.data() + prefix_part.size()) return false;
    return prefix >= 0 && prefix <= 32;
}

bool ListParser::is_cidr_v6(std::string_view s) {
    auto slash = s.find('/');
    if (slash == std::string_view::npos) return false;
    auto ip_part = s.substr(0, slash);
    auto prefix_part = s.substr(slash + 1);
    if (!is_ipv6(ip_part)) return false;
    if (prefix_part.empty() || prefix_part.size() > 3) return false;
    int prefix = 0;
    auto [ptr, ec] = std::from_chars(prefix_part.data(), prefix_part.data() + prefix_part.size(), prefix);
    if (ec != std::errc{} || ptr != prefix_part.data() + prefix_part.size()) return false;
    return prefix >= 0 && prefix <= 128;
}

bool ListParser::is_domain(std::string_view s) {
    if (s.empty()) return false;
    // Strip leading wildcard prefix
    if (s.size() >= 2 && s[0] == '*' && s[1] == '.') {
        s.remove_prefix(2);
    }
    if (s.empty()) return false;
    // Domain: labels separated by dots, each label is alphanumeric or hyphen
    bool has_alpha = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            has_alpha = true;
        } else if ((c >= '0' && c <= '9') || c == '-' || c == '_') {
            // ok (hyphen/underscore allowed in labels)
        } else if (c == '.') {
            // dot separating labels
            if (i == 0 || s[i - 1] == '.') return false; // no empty labels
        } else {
            return false;
        }
    }
    // Must contain at least one alpha character to distinguish from IPv4
    return has_alpha;
}

bool ListParser::classify_entry(std::string_view entry, ListEntryVisitor& visitor) {
    if (is_cidr_v4(entry) || is_cidr_v6(entry)) {
        visitor.on_entry(EntryType::Cidr, entry);
        return true;
    }
    if (is_ipv4(entry) || is_ipv6(entry)) {
        visitor.on_entry(EntryType::Ip, entry);
        return true;
    }
    if (is_domain(entry)) {
        visitor.on_entry(EntryType::Domain, entry);
        return true;
    }
    return false;
}

void ListParser::stream_parse(std::istream& input, ListEntryVisitor& visitor) {
    std::string line;
    while (std::getline(input, line)) {
        auto sv = trim(std::string_view(line));
        if (sv.empty()) continue;
        if (sv.front() == '#') continue;
        classify_entry(sv, visitor);
    }
}

} // namespace keen_pbr3
