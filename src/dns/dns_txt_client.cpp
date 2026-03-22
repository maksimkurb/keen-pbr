#include "dns_txt_client.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <cctype>
#include <cstdint>
#include <mutex>
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

    if (parsed_server.ip.find(':') != std::string::npos) {
        if (error_out) *error_out = "IPv6 resolver addresses are not supported by this resolver backend";
        return std::nullopt;
    }

    static std::mutex resolver_mutex;
    std::lock_guard<std::mutex> guard(resolver_mutex);

    const struct __res_state saved_resolver_state = _res;
    auto restore_state = [&saved_resolver_state]() {
        _res = saved_resolver_state;
    };

    if (res_init() != 0) {
        if (error_out) *error_out = "res_init failed";
        restore_state();
        return std::nullopt;
    }

    _res.nscount = 1;
    _res.retry = 1;
    _res.retrans = static_cast<int>(std::max<int64_t>(1, timeout.count() / 1000));
    _res.nsaddr_list[0].sin_family = AF_INET;
    _res.nsaddr_list[0].sin_port = htons(parsed_server.port);
    if (inet_pton(AF_INET, parsed_server.ip.c_str(), &_res.nsaddr_list[0].sin_addr) != 1) {
        if (error_out) *error_out = "Invalid IPv4 DNS resolver address";
        restore_state();
        return std::nullopt;
    }

    std::array<unsigned char, NS_PACKETSZ * 8> response {};
    const int response_len = res_query(domain.c_str(),
                                       ns_c_in,
                                       ns_t_txt,
                                       response.data(),
                                       static_cast<int>(response.size()));
    restore_state();
    if (response_len < 0) {
        if (error_out) *error_out = "DNS TXT query failed";
        return std::nullopt;
    }

    return parse_first_txt_answer(response.data(), response_len, error_out);
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
