#ifndef KEEN_PBR3_CONFIG_ADDR_SPEC_HPP
#define KEEN_PBR3_CONFIG_ADDR_SPEC_HPP
#include <arpa/inet.h>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct AddrSpec {
    std::vector<std::string> addrs;
    bool negate = false;
};

// Throws std::invalid_argument if cidr is not a valid IPv4 or IPv6 address
// with an optional prefix length (e.g. "192.168.1.0/24", "2001:db8::/32",
// "10.0.0.1" (bare host address)).
inline void validate_cidr(const std::string& cidr) {
    auto slash = cidr.find('/');
    const std::string ip_part = (slash == std::string::npos) ? cidr : cidr.substr(0, slash);

    char buf[16];
    const bool is_v4 = inet_pton(AF_INET,  ip_part.c_str(), buf) == 1;
    const bool is_v6 = !is_v4 && inet_pton(AF_INET6, ip_part.c_str(), buf) == 1;

    if (!is_v4 && !is_v6) {
        throw std::invalid_argument("Invalid IP address in CIDR spec: '" + cidr + "'");
    }

    if (slash != std::string::npos) {
        const std::string prefix_str = cidr.substr(slash + 1);
        int prefix = -1;
        auto [ptr, ec] = std::from_chars(
            prefix_str.data(), prefix_str.data() + prefix_str.size(), prefix);
        if (ec != std::errc{} || ptr != prefix_str.data() + prefix_str.size()) {
            throw std::invalid_argument("Invalid prefix length in CIDR spec: '" + cidr + "'");
        }
        const int max_prefix = is_v4 ? 32 : 128;
        if (prefix < 0 || prefix > max_prefix) {
            throw std::invalid_argument("Prefix length out of range in CIDR spec: '" + cidr + "'");
        }
    }
}

// Parse a comma-separated CIDR spec with optional leading "!" negation.
// "192.168.1.0/24,10.0.0.0/8" → {{"192.168.1.0/24","10.0.0.0/8"}, false}
// "!192.168.1.0/24"            → {{"192.168.1.0/24"}, true}
// ""                            → {{}, false}
inline AddrSpec parse_addr_spec(const std::string& spec) {
    if (spec.empty()) return {};
    AddrSpec result;
    result.negate = (spec[0] == '!');
    const std::string body = result.negate ? spec.substr(1) : spec;
    std::istringstream ss(body);
    std::string token;
    while (std::getline(ss, token, ',')) {
        auto start = token.find_first_not_of(" \t");
        auto end   = token.find_last_not_of(" \t");
        if (start == std::string::npos) continue; // all-whitespace token
        token = token.substr(start, end - start + 1);
        validate_cidr(token);
        result.addrs.push_back(std::move(token));
    }
    return result;
}

} // namespace keen_pbr3

#endif // KEEN_PBR3_CONFIG_ADDR_SPEC_HPP
