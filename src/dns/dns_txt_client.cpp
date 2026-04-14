#include "dns_txt_client.hpp"

#include "../log/logger.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <resolv.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include "dns_server.hpp"

namespace keen_pbr3 {

namespace {

constexpr const char* kDnsTxtAnswerNotFound = "DNS TXT answer not found";

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

std::string strip_balanced_quotes(std::string value) {
    value = trim_copy(value);
    while (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        const bool wrapped_by_double = (first == '"' && last == '"');
        const bool wrapped_by_single = (first == '\'' && last == '\'');
        if (!wrapped_by_double && !wrapped_by_single) {
            break;
        }
        value = trim_copy(value.substr(1, value.size() - 2));
    }
    return value;
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
    std::optional<std::string> first_txt;
    std::optional<std::string> latest_ts_txt;
    std::optional<std::int64_t> latest_ts_value;
    std::vector<std::string> txt_records_log_lines;

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

        if (!first_txt.has_value()) {
            first_txt = txt;
        }

        const ResolverConfigHashTxtValue parsed = parse_resolver_config_hash_txt(txt);
        txt_records_log_lines.push_back(
            std::string("#") + std::to_string(i) + " txt=\"" + txt +
            "\" ts=" + (parsed.ts.has_value() ? std::to_string(*parsed.ts) : "none") +
            " hash=" + parsed.hash);
        if (parsed.ts.has_value() &&
            (!latest_ts_value.has_value() || *parsed.ts > *latest_ts_value)) {
            latest_ts_value = parsed.ts;
            latest_ts_txt = txt;
        }
    }

    if (!txt_records_log_lines.empty()) {
        std::string records_joined;
        for (size_t i = 0; i < txt_records_log_lines.size(); ++i) {
            if (i > 0) {
                records_joined += " ; ";
            }
            records_joined += txt_records_log_lines[i];
        }
        Logger::instance().verbose("Resolver TXT answers: {}", records_joined);
    } else {
        Logger::instance().verbose("Resolver TXT answers: <none>");
    }

    if (latest_ts_txt.has_value()) {
        const ResolverConfigHashTxtValue parsed = parse_resolver_config_hash_txt(*latest_ts_txt);
        Logger::instance().verbose("Resolver TXT selected by latest ts: txt=\"{}\" ts={} hash={}",
                                   *latest_ts_txt,
                                   parsed.ts.has_value() ? std::to_string(*parsed.ts) : "none",
                                   parsed.hash);
        return latest_ts_txt;
    }
    if (first_txt.has_value()) {
        const ResolverConfigHashTxtValue parsed = parse_resolver_config_hash_txt(*first_txt);
        Logger::instance().verbose("Resolver TXT selected by first answer: txt=\"{}\" ts={} hash={}",
                                   *first_txt,
                                   parsed.ts.has_value() ? std::to_string(*parsed.ts) : "none",
                                   parsed.hash);
        return first_txt;
    }

    if (error_out) *error_out = kDnsTxtAnswerNotFound;
    return std::nullopt;
}

} // namespace

std::optional<std::string> query_dns_txt_record(const std::string& dns_server_address,
                                                const std::string& domain,
                                                std::chrono::milliseconds timeout,
                                                std::string* error_out) {
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("dns_txt_query_start",
                             "resolver={} domain={} timeout_ms={}",
                             dns_server_address,
                             domain,
                             timeout.count());
    if (domain.empty()) {
        if (error_out) *error_out = "DNS TXT query domain is empty";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=empty_domain",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        return std::nullopt;
    }

    ParsedDnsAddress parsed_server = parse_dns_address_str(dns_server_address);

    if (parsed_server.ip.find(':') != std::string::npos) {
        if (error_out) *error_out = "IPv6 resolver addresses are not supported by this resolver backend";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=ipv6_unsupported",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        return std::nullopt;
    }

    sockaddr_in resolver_addr {};
    resolver_addr.sin_family = AF_INET;
    resolver_addr.sin_port = htons(parsed_server.port);
    if (inet_pton(AF_INET, parsed_server.ip.c_str(), &resolver_addr.sin_addr) != 1) {
        if (error_out) *error_out = "Invalid IPv4 DNS resolver address";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=invalid_ipv4",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        return std::nullopt;
    }

    std::array<unsigned char, NS_PACKETSZ * 2> query {};
    const int query_len = res_mkquery(ns_o_query,
                                      domain.c_str(),
                                      ns_c_in,
                                      ns_t_txt,
                                      nullptr,
                                      0,
                                      nullptr,
                                      query.data(),
                                      static_cast<int>(query.size()));
    if (query_len < 0) {
        if (error_out) *error_out = "Failed to build DNS TXT query";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=build_query_failed",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        return std::nullopt;
    }

    const int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        if (error_out) *error_out = "Failed to create DNS socket";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=socket_failed",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        return std::nullopt;
    }
    const auto close_socket = [socket_fd]() { close(socket_fd); };

    const auto timeout_ms = std::max<int64_t>(1, timeout.count());
    timeval socket_timeout {};
    socket_timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    socket_timeout.tv_usec = static_cast<decltype(socket_timeout.tv_usec)>((timeout_ms % 1000) * 1000);

    if (setsockopt(socket_fd,
                   SOL_SOCKET,
                   SO_SNDTIMEO,
                   &socket_timeout,
                   sizeof(socket_timeout)) != 0 ||
        setsockopt(socket_fd,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &socket_timeout,
                   sizeof(socket_timeout)) != 0) {
        if (error_out) *error_out = "Failed to configure DNS socket timeout";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=setsockopt_failed",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        close_socket();
        return std::nullopt;
    }

    std::array<unsigned char, NS_PACKETSZ * 8> response {};
    const ssize_t sent = sendto(socket_fd,
                                query.data(),
                                static_cast<size_t>(query_len),
                                0,
                                reinterpret_cast<const sockaddr*>(&resolver_addr),
                                sizeof(resolver_addr));
    if (sent != static_cast<ssize_t>(query_len)) {
        if (error_out) *error_out = "Failed to send DNS TXT query";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=send_failed",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        close_socket();
        return std::nullopt;
    }

    const ssize_t response_len = recvfrom(socket_fd,
                                          response.data(),
                                          response.size(),
                                          0,
                                          nullptr,
                                          nullptr);
    if (response_len <= 0) {
        if (error_out) *error_out = "DNS TXT query failed";
        Logger::instance().trace("dns_txt_query_error",
                                 "resolver={} domain={} duration_ms={} error=recv_failed",
                                 dns_server_address,
                                 domain,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count());
        close_socket();
        return std::nullopt;
    }

    if (response_len >= NS_HFIXEDSZ) {
        const auto* response_header = reinterpret_cast<const HEADER*>(response.data());
        if (response_header->tc != 0) {
            if (error_out) *error_out = "DNS TXT response truncated; TCP fallback is not implemented";
            Logger::instance().trace("dns_txt_query_error",
                                     "resolver={} domain={} duration_ms={} error=truncated_response",
                                     dns_server_address,
                                     domain,
                                     std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - started_at).count());
            close_socket();
            return std::nullopt;
        }
    }

    close_socket();
    auto result = parse_first_txt_answer(response.data(), static_cast<int>(response_len), error_out);
    Logger::instance().trace(result.has_value() ? "dns_txt_query_end" : "dns_txt_query_error",
                             "resolver={} domain={} duration_ms={} bytes={} success={}",
                             dns_server_address,
                             domain,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started_at).count(),
                             response_len,
                             result.has_value() ? "true" : "false");
    return result;
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

ResolverConfigHashTxtValue parse_resolver_config_hash_txt(const std::string& txt_payload) {
    ResolverConfigHashTxtValue value;
    const std::string normalized = strip_balanced_quotes(txt_payload);

    const size_t delimiter = normalized.find('|');
    if (delimiter == std::string::npos) {
        value.hash = normalize_dns_txt_md5(normalized);
        return value;
    }

    const std::string ts_part = trim_copy(normalized.substr(0, delimiter));
    const std::string hash_part = trim_copy(normalized.substr(delimiter + 1));
    value.hash = normalize_dns_txt_md5(hash_part);

    if (!ts_part.empty() &&
        std::all_of(ts_part.begin(), ts_part.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        try {
            value.ts = std::stoll(ts_part);
        } catch (...) {
            value.ts = std::nullopt;
        }
    }

    return value;
}

bool is_valid_resolver_config_hash_txt_value(const ResolverConfigHashTxtValue& value) {
    if (value.hash.size() != 32) {
        return false;
    }
    return std::all_of(value.hash.begin(), value.hash.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

ResolverConfigHashProbeResult query_resolver_config_hash_txt(const std::string& dns_server_address,
                                                             const std::string& domain,
                                                             std::chrono::milliseconds timeout) {
    ResolverConfigHashProbeResult result;
    try {
        auto txt = query_dns_txt_record(dns_server_address, domain, timeout, &result.error);
        if (!txt.has_value()) {
            result.status = (result.error == kDnsTxtAnswerNotFound)
                ? ResolverConfigHashProbeStatus::NO_USABLE_TXT
                : ResolverConfigHashProbeStatus::QUERY_FAILED;
            return result;
        }

        result.raw_txt = *txt;
        result.parsed_value = parse_resolver_config_hash_txt(*txt);
        result.status = is_valid_resolver_config_hash_txt_value(result.parsed_value)
            ? ResolverConfigHashProbeStatus::SUCCESS
            : ResolverConfigHashProbeStatus::INVALID_TXT;
        if (result.status == ResolverConfigHashProbeStatus::INVALID_TXT) {
            result.error = "Resolver TXT payload is missing a valid md5 hash";
        }
        return result;
    } catch (const std::exception& e) {
        result.status = ResolverConfigHashProbeStatus::QUERY_FAILED;
        result.error = e.what();
        return result;
    }
}

} // namespace keen_pbr3
