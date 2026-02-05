#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

struct ParsedList {
    std::vector<std::string> ips;     // Individual IP addresses (v4 and v6)
    std::vector<std::string> cidrs;   // CIDR subnets (v4 and v6)
    std::vector<std::string> domains; // Domain names (including wildcards like *.example.com)
};

class ListParser {
public:
    // Parse a multi-line text containing a mix of IPs, CIDRs, and domains.
    // Skips empty lines and comment lines (starting with #).
    static ParsedList parse(const std::string& text);

private:
    static bool is_ipv4(std::string_view s);
    static bool is_ipv6(std::string_view s);
    static bool is_cidr_v4(std::string_view s);
    static bool is_cidr_v6(std::string_view s);
    static bool is_domain(std::string_view s);
};

} // namespace keen_pbr3
