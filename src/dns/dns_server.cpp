#include "dns_server.hpp"

#include <arpa/inet.h>

#include <charconv>
#include <cstdint>
#include <string_view>

namespace keen_pbr3 {

namespace {

uint16_t parse_dns_port_or_throw(const std::string& address, std::string_view port_str) {
    uint32_t port = 0;
    auto [ptr, ec] = std::from_chars(port_str.data(),
                                     port_str.data() + port_str.size(),
                                     port);
    if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
        throw DnsError("Invalid DNS server address: '" + address +
                       "' (non-numeric port)");
    }
    if (port < 1 || port > 65535) {
        throw DnsError("Invalid DNS server address: '" + address +
                       "' (port out of range 1-65535)");
    }
    return static_cast<uint16_t>(port);
}

bool is_valid_ipv4(const std::string& addr) {
    int dots = 0;
    size_t start = 0;
    for (size_t i = 0; i <= addr.size(); ++i) {
        if (i == addr.size() || addr[i] == '.') {
            if (i == start) return false;
            uint32_t octet = 0;
            auto [ptr, ec] = std::from_chars(addr.data() + start, addr.data() + i, octet);
            if (ec != std::errc{} || ptr != addr.data() + i) return false;
            if (octet > 255) return false;
            if (i < addr.size()) ++dots;
            start = i + 1;
        } else if (addr[i] < '0' || addr[i] > '9') {
            return false;
        }
    }
    return dots == 3;
}

bool is_valid_ipv6(const std::string& addr) {
    if (addr.empty()) return false;

    // Scoped IPv6 zone IDs ("%eth0", "%1") are intentionally rejected here.
    // The rest of the codebase treats addresses as plain numeric literals.
    if (addr.find('%') != std::string::npos) return false;

    in6_addr parsed{};
    return inet_pton(AF_INET6, addr.c_str(), &parsed) == 1;
}

bool is_valid_ip(const std::string& addr) {
    return is_valid_ipv4(addr) || is_valid_ipv6(addr);
}

} // namespace

ParsedDnsAddress parse_dns_address_str(const std::string& address) {
    if (address.empty()) {
        throw DnsError("Invalid DNS server address: empty string");
    }

    std::string ip;
    uint16_t    port = 53;

    if (address[0] == '[') {
        // Bracketed IPv6: "[::1]" or "[::1]:port"
        auto close = address.find(']');
        if (close == std::string::npos) {
            throw DnsError("Invalid DNS server address: '" + address +
                           "' (missing closing ']')");
        }
        ip = address.substr(1, close - 1);
        if (close + 1 < address.size()) {
            if (address[close + 1] != ':') {
                throw DnsError("Invalid DNS server address: '" + address +
                               "' (expected ':' after ']')");
            }
            port = parse_dns_port_or_throw(address,
                                           std::string_view(address).substr(close + 2));
        }
    } else {
        int colon_count = 0;
        for (char c : address) {
            if (c == ':') ++colon_count;
        }
        if (colon_count == 1) {
            // IPv4:port
            auto sep = address.find(':');
            ip = address.substr(0, sep);
            port = parse_dns_port_or_throw(address,
                                           std::string_view(address).substr(sep + 1));
        } else {
            // Bare IPv4 or IPv6 without an explicit port.
            ip   = address;
            port = 53;
        }
    }

    if (!is_valid_ip(ip)) {
        throw DnsError("Invalid DNS server address: '" + address +
                       "' (not a valid IPv4 or IPv6 address)");
    }

    return {ip, port};
}

DohUrl parse_doh_url(const std::string& url, bool allow_plain_http) {
    const auto fail = [&](const char* why) -> DohUrl {
        throw DnsError("Invalid DNS-over-HTTPS URL: '" + url + "' (" + why + ")");
    };
    DohUrl out;
    std::string_view rest(url);
    if (rest.substr(0, 8) == "https://") {
        rest.remove_prefix(8);
    } else if (allow_plain_http && rest.substr(0, 7) == "http://") {
        rest.remove_prefix(7);
        out.secure = false;
        out.port = 80;
    } else {
        return fail("must start with https://");
    }
    for (const char c : rest) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F || c == '#') {
            return fail("contains spaces, control characters or a fragment");
        }
    }

    const auto path_start = rest.find_first_of("/?");
    const std::string_view authority = rest.substr(0, path_start);
    if (path_start != std::string_view::npos) {
        out.path = std::string(rest.substr(path_start));
        if (out.path.front() == '?') {
            out.path.insert(out.path.begin(), '/');
        }
    }
    if (authority.empty()) {
        return fail("host is missing");
    }
    if (authority.find('@') != std::string_view::npos) {
        return fail("user name and password are not supported");
    }

    std::string_view host = authority;
    std::string_view port_text;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return fail("missing closing ']'");
        }
        host = authority.substr(1, close - 1);
        const auto after = authority.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return fail("expected ':' after ']'");
            }
            port_text = after.substr(1);
        }
        if (!is_valid_ipv6(std::string(host))) {
            return fail("not a valid IPv6 address");
        }
        out.ip = std::string(host);
    } else {
        const auto colon = authority.find(':');
        if (colon != std::string_view::npos) {
            host = authority.substr(0, colon);
            port_text = authority.substr(colon + 1);
            if (port_text.find(':') != std::string_view::npos) {
                return fail("IPv6 addresses must be written in brackets");
            }
        }
        if (host.empty()) {
            return fail("host is missing");
        }
        if (is_valid_ipv4(std::string(host))) {
            out.ip = std::string(host);
        } else {
            for (const char c : host) {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
                if (!ok) {
                    return fail("host is not a valid name");
                }
            }
        }
    }
    out.host = std::string(host);
    if (!port_text.empty()) {
        uint32_t port = 0;
        auto [ptr, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (ec != std::errc{} || ptr != port_text.data() + port_text.size() || port < 1 ||
            port > 65535) {
            return fail("port must be a number from 1 to 65535");
        }
        out.port = static_cast<uint16_t>(port);
    } else if (authority.back() == ':') {
        return fail("port is empty");
    }
    return out;
}

DnsServerConfig parse_doh_server(const std::string& tag,
                                 const std::string& url,
                                 const std::optional<std::string>& detour,
                                 const std::vector<std::string>& ips) {
    const DohUrl parsed = parse_doh_url(url);

    DnsServerConfig config;
    config.tag     = tag;
    config.address = url;
    config.detour  = detour;
    config.port    = parsed.port;
    config.doh_url = url;
    config.ips     = ips;
    return config;
}

void validate_dns_address(const std::string& address) {
    parse_dns_address_str(address); // throws on any error
}

DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour) {
    auto parsed = parse_dns_address_str(address);

    DnsServerConfig config;
    config.tag         = tag;
    config.address     = address;
    config.detour      = detour;
    config.resolved_ip = parsed.ip;
    config.port        = parsed.port;

    return config;
}

} // namespace keen_pbr3
