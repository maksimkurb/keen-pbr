#include "dns_txt_client.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "dns_server.hpp"

namespace keen_pbr3 {

namespace {

constexpr size_t DNS_HEADER_SIZE = 12;
constexpr uint16_t DNS_TYPE_TXT = 16;
constexpr uint16_t DNS_CLASS_IN = 1;

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t read_u32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

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

size_t skip_dns_name(const std::vector<uint8_t>& packet, size_t pos) {
    size_t cursor = pos;
    size_t jumps = 0;
    while (true) {
        if (cursor >= packet.size()) {
            throw DnsError("DNS name truncated");
        }

        uint8_t len = packet[cursor];
        if (len == 0) {
            return (cursor - pos) + 1;
        }
        if ((len & 0xC0) == 0xC0) {
            if (cursor + 1 >= packet.size()) {
                throw DnsError("DNS compression pointer truncated");
            }
            return (cursor - pos) + 2;
        }
        if ((len & 0xC0) != 0) {
            throw DnsError("DNS name label invalid");
        }

        cursor += 1 + len;
        if (++jumps > 128) {
            throw DnsError("DNS name parse exceeded limit");
        }
    }
}

std::vector<uint8_t> build_txt_query(const std::string& domain) {
    if (domain.empty()) {
        throw DnsError("DNS TXT query domain is empty");
    }

    std::vector<uint8_t> out;
    out.reserve(512);

    // Header
    append_u16(out, 0x4B50); // ID
    append_u16(out, 0x0100); // RD
    append_u16(out, 1);      // QDCOUNT
    append_u16(out, 0);      // ANCOUNT
    append_u16(out, 0);      // NSCOUNT
    append_u16(out, 0);      // ARCOUNT

    size_t start = 0;
    while (start < domain.size()) {
        size_t dot = domain.find('.', start);
        if (dot == std::string::npos) {
            dot = domain.size();
        }
        const size_t len = dot - start;
        if (len == 0 || len > 63) {
            throw DnsError("Invalid DNS label in domain: " + domain);
        }
        out.push_back(static_cast<uint8_t>(len));
        for (size_t i = start; i < dot; ++i) {
            out.push_back(static_cast<uint8_t>(domain[i]));
        }
        start = dot + 1;
    }
    out.push_back(0); // end name

    append_u16(out, DNS_TYPE_TXT);
    append_u16(out, DNS_CLASS_IN);

    return out;
}

std::optional<std::string> parse_first_txt_answer(const std::vector<uint8_t>& packet,
                                                  std::string* error_out) {
    if (packet.size() < DNS_HEADER_SIZE) {
        if (error_out) *error_out = "DNS response header truncated";
        return std::nullopt;
    }

    const uint16_t flags = read_u16(packet.data() + 2);
    const uint8_t rcode = static_cast<uint8_t>(flags & 0x000F);
    if (rcode != 0) {
        if (error_out) *error_out = "DNS TXT query failed with rcode=" + std::to_string(rcode);
        return std::nullopt;
    }

    const uint16_t qdcount = read_u16(packet.data() + 4);
    const uint16_t ancount = read_u16(packet.data() + 6);

    size_t pos = DNS_HEADER_SIZE;
    for (uint16_t i = 0; i < qdcount; ++i) {
        const size_t name_len = skip_dns_name(packet, pos);
        pos += name_len;
        if (pos + 4 > packet.size()) {
            if (error_out) *error_out = "DNS question section truncated";
            return std::nullopt;
        }
        pos += 4;
    }

    for (uint16_t i = 0; i < ancount; ++i) {
        const size_t name_len = skip_dns_name(packet, pos);
        pos += name_len;
        if (pos + 10 > packet.size()) {
            if (error_out) *error_out = "DNS answer section truncated";
            return std::nullopt;
        }

        const uint16_t type = read_u16(packet.data() + pos);
        pos += 2;
        const uint16_t klass = read_u16(packet.data() + pos);
        pos += 2;
        (void)read_u32(packet.data() + pos); // ttl
        pos += 4;
        const uint16_t rdlen = read_u16(packet.data() + pos);
        pos += 2;

        if (pos + rdlen > packet.size()) {
            if (error_out) *error_out = "DNS answer payload truncated";
            return std::nullopt;
        }

        if (type == DNS_TYPE_TXT && klass == DNS_CLASS_IN && rdlen > 0) {
            size_t txt_pos = pos;
            size_t txt_end = pos + rdlen;
            std::string txt;
            while (txt_pos < txt_end) {
                const uint8_t len = packet[txt_pos++];
                if (txt_pos + len > txt_end) {
                    if (error_out) *error_out = "DNS TXT chunk truncated";
                    return std::nullopt;
                }
                txt.append(reinterpret_cast<const char*>(packet.data() + txt_pos), len);
                txt_pos += len;
            }
            return txt;
        }

        pos += rdlen;
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
    const std::vector<uint8_t> request = build_txt_query(domain);

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
                                request.data(),
                                request.size(),
                                0,
                                reinterpret_cast<const sockaddr*>(addr_storage.data()),
                                addr_len);
    if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
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

    std::array<uint8_t, 4096> buffer{};
    const ssize_t n = recv(sock, buffer.data(), buffer.size(), 0);
    if (n <= 0) {
        if (error_out) *error_out = "recv() failed: " + std::string(strerror(errno));
        close_sock();
        return std::nullopt;
    }

    close_sock();

    std::vector<uint8_t> response(buffer.begin(), buffer.begin() + n);
    return parse_first_txt_answer(response, error_out);
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
