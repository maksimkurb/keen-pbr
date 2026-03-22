#include "dns_txt_client.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <resolv.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

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

int build_txt_query_packet(const std::string& domain,
                           std::array<unsigned char, NS_PACKETSZ * 2>& out,
                           std::string* error_out) {
    if (domain.empty()) {
        if (error_out) *error_out = "DNS TXT query domain is empty";
        return -1;
    }

    const int query_len = res_mkquery(ns_o_query,
                                      domain.c_str(),
                                      ns_c_in,
                                      ns_t_txt,
                                      nullptr,
                                      0,
                                      nullptr,
                                      out.data(),
                                      static_cast<int>(out.size()));
    if (query_len < 0) {
        if (error_out) *error_out = "res_mkquery failed";
        return -1;
    }

    return query_len;
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
    ParsedDnsAddress server = parse_dns_address_str(dns_server_address);

    std::array<unsigned char, NS_PACKETSZ * 2> query{};
    const int query_len = build_txt_query_packet(domain, query, error_out);
    if (query_len < 0) {
        return std::nullopt;
    }

    int family = AF_INET;
    std::array<uint8_t, sizeof(sockaddr_in6)> addr_storage{};
    socklen_t addr_len = 0;

    if (server.ip.find(':') != std::string::npos) {
        family = AF_INET6;
        sockaddr_in6 addr6{};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(server.port);
        if (inet_pton(AF_INET6, server.ip.c_str(), &addr6.sin6_addr) != 1) {
            if (error_out) *error_out = "Invalid IPv6 DNS resolver address";
            return std::nullopt;
        }
        std::memcpy(addr_storage.data(), &addr6, sizeof(addr6));
        addr_len = static_cast<socklen_t>(sizeof(addr6));
    } else {
        sockaddr_in addr4{};
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(server.port);
        if (inet_pton(AF_INET, server.ip.c_str(), &addr4.sin_addr) != 1) {
            if (error_out) *error_out = "Invalid IPv4 DNS resolver address";
            return std::nullopt;
        }
        std::memcpy(addr_storage.data(), &addr4, sizeof(addr4));
        addr_len = static_cast<socklen_t>(sizeof(addr4));
    }

    const int sock = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        if (error_out) *error_out = "socket() failed: " + std::string(strerror(errno));
        return std::nullopt;
    }

    auto close_sock = [&]() {
        close(sock);
    };

    const ssize_t sent = sendto(sock,
                                query.data(),
                                static_cast<size_t>(query_len),
                                0,
                                reinterpret_cast<const sockaddr*>(addr_storage.data()),
                                addr_len);
    if (sent < 0 || sent != query_len) {
        if (error_out) *error_out = "sendto() failed: " + std::string(strerror(errno));
        close_sock();
        return std::nullopt;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);

    const int rc = select(sock + 1, &read_fds, nullptr, nullptr, &tv);
    if (rc <= 0) {
        if (error_out) {
            *error_out = (rc == 0) ? "DNS TXT query timeout"
                                   : "select() failed: " + std::string(strerror(errno));
        }
        close_sock();
        return std::nullopt;
    }

    std::array<unsigned char, NS_PACKETSZ * 8> response{};
    const ssize_t n = recv(sock, response.data(), response.size(), 0);
    if (n <= 0) {
        if (error_out) *error_out = "recv() failed: " + std::string(strerror(errno));
        close_sock();
        return std::nullopt;
    }

    close_sock();
    return parse_first_txt_answer(response.data(), static_cast<int>(n), error_out);
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
