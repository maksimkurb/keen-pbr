#include "dns_txt_client.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <cctype>
#include <cstdint>
#include <netinet/in.h>
#include <resolv.h>

#include "dns_server.hpp"

namespace keen_pbr3 {

namespace {

bool is_hex_char(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

std::string trim_copy(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::optional<std::string> parse_first_txt_answer(const unsigned char* response,
                                                  int response_len,
                                                  std::string* error_out) {
    ns_msg handle {};
    if (ns_initparse(response, response_len, &handle) < 0) {
        if (error_out) *error_out = "Failed to parse DNS response";
        return std::nullopt;
    }

    const int answer_count = ns_msg_count(handle, ns_s_an);
    for (int i = 0; i < answer_count; ++i) {
        ns_rr rr {};
        if (ns_parserr(&handle, ns_s_an, i, &rr) < 0) {
            continue;
        }
        if (ns_rr_type(rr) != ns_t_txt || ns_rr_class(rr) != ns_c_in) {
            continue;
        }

        const unsigned char* rdata = ns_rr_rdata(rr);
        const int rdlen = ns_rr_rdlen(rr);
        if (rdlen <= 0) {
            continue;
        }

        std::string txt;
        int offset = 0;
        while (offset < rdlen) {
            const unsigned char chunk_len = rdata[offset++];
            if (offset + chunk_len > rdlen) {
                if (error_out) *error_out = "DNS TXT chunk truncated";
                return std::nullopt;
            }
            txt.append(reinterpret_cast<const char*>(rdata + offset), chunk_len);
            offset += chunk_len;
        }
        return txt;
    }

    if (error_out) *error_out = "DNS TXT answer not found";
    return std::nullopt;
}

void configure_resolver_server(res_state resolver_state,
                               const ParsedDnsAddress& parsed_server,
                               sockaddr_in6* ipv6_storage) {
    resolver_state->nscount = 1;
    resolver_state->retry = 1;

    if (parsed_server.ip.find(':') == std::string::npos) {
        resolver_state->nsaddr_list[0].sin_family = AF_INET;
        resolver_state->nsaddr_list[0].sin_port = htons(parsed_server.port);
        if (inet_pton(AF_INET,
                      parsed_server.ip.c_str(),
                      &resolver_state->nsaddr_list[0].sin_addr) != 1) {
            throw DnsError("Invalid IPv4 DNS resolver address: " + parsed_server.ip);
        }
        resolver_state->_u._ext.nscount = 0;
        resolver_state->_u._ext.nsaddrs[0] = nullptr;
        return;
    }

    if (ipv6_storage == nullptr) {
        throw DnsError("Internal DNS resolver setup error");
    }

    *ipv6_storage = {};
    ipv6_storage->sin6_family = AF_INET6;
    ipv6_storage->sin6_port = htons(parsed_server.port);
    if (inet_pton(AF_INET6, parsed_server.ip.c_str(), &ipv6_storage->sin6_addr) != 1) {
        throw DnsError("Invalid IPv6 DNS resolver address: " + parsed_server.ip);
    }

    // Keep an IPv4 placeholder for slot 0 and point the extended resolver table
    // at the real IPv6 server address.
    resolver_state->nsaddr_list[0].sin_family = AF_INET;
    resolver_state->nsaddr_list[0].sin_port = htons(parsed_server.port);
    resolver_state->nsaddr_list[0].sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    resolver_state->_u._ext.nscount = 1;
    resolver_state->_u._ext.nsmap[0] = 0;
    resolver_state->_u._ext.nsaddrs[0] = ipv6_storage;
}

} // namespace

std::optional<std::string> query_dns_txt_record(const std::string& dns_server_address,
                                                const std::string& domain,
                                                std::chrono::milliseconds timeout,
                                                std::string* error_out) {
    if (domain.empty()) {
        if (error_out) *error_out = "DNS TXT query domain is empty";
        return std::nullopt;
    }

    ParsedDnsAddress parsed_server = parse_dns_address_str(dns_server_address);

    struct __res_state resolver_state_storage {};
    res_state resolver_state = &resolver_state_storage;
    if (res_ninit(resolver_state) != 0) {
        if (error_out) *error_out = "res_ninit failed";
        return std::nullopt;
    }

    sockaddr_in6 ipv6_server {};
    try {
        configure_resolver_server(resolver_state, parsed_server, &ipv6_server);

        const int timeout_sec = static_cast<int>(std::max<int64_t>(1, timeout.count() / 1000));
        resolver_state->retrans = timeout_sec;

        std::array<unsigned char, NS_PACKETSZ * 8> response {};
        const int response_len = res_nquery(resolver_state,
                                            domain.c_str(),
                                            ns_c_in,
                                            ns_t_txt,
                                            response.data(),
                                            static_cast<int>(response.size()));
        if (response_len < 0) {
            if (error_out) *error_out = "DNS TXT query failed";
            res_nclose(resolver_state);
            return std::nullopt;
        }

        auto parsed = parse_first_txt_answer(response.data(), response_len, error_out);
        res_nclose(resolver_state);
        return parsed;
    } catch (...) {
        res_nclose(resolver_state);
        throw;
    }
}

std::string normalize_dns_txt_md5(const std::string& txt_payload) {
    std::string normalized = trim_copy(txt_payload);

    while (normalized.size() >= 2) {
        const char first = normalized.front();
        const char last = normalized.back();
        const bool wrapped_by_double = (first == '"' && last == '"');
        const bool wrapped_by_single = (first == '\'' && last == '\'');
        if (!wrapped_by_double && !wrapped_by_single) {
            break;
        }
        normalized = trim_copy(normalized.substr(1, normalized.size() - 2));
    }

    std::string compact;
    compact.reserve(normalized.size());
    for (char c : normalized) {
        if (c == '"' || c == '\'' || std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        compact.push_back(c);
    }

    const std::string md5_prefix = "md5=";
    if (compact.size() > md5_prefix.size()) {
        std::string lower_prefix = compact.substr(0, md5_prefix.size());
        std::transform(lower_prefix.begin(), lower_prefix.end(), lower_prefix.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower_prefix == md5_prefix) {
            compact = compact.substr(md5_prefix.size());
        }
    }

    std::string hex_only;
    hex_only.reserve(compact.size());
    for (char c : compact) {
        if (is_hex_char(c)) {
            hex_only.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (hex_only.size() == 32) {
        return hex_only;
    }

    return compact;
}

} // namespace keen_pbr3
