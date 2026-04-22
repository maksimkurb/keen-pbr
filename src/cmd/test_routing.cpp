#include "test_routing.hpp"

#include "../config/routing_state.hpp"
#include "../dns/dns_server.hpp"
#include "../lists/ipset.hpp"
#include "../lists/kernel_set_tester.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/format_compat.hpp"
#include "../util/firewall_backend_utils.hpp"
#include "../util/string_compat.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <set>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace keen_pbr3 {

namespace {

bool is_ipv4_address(const std::string& s) {
    struct in_addr addr;
    return inet_pton(AF_INET, s.c_str(), &addr) == 1;
}

bool is_ipv6_address(const std::string& s) {
    struct in6_addr addr;
    return inet_pton(AF_INET6, s.c_str(), &addr) == 1;
}

bool is_ip_address(const std::string& s) {
    return is_ipv4_address(s) || is_ipv6_address(s);
}

// "www.google.com" → ["www.google.com", "google.com", "com"]
std::vector<std::string> domain_candidates(const std::string& domain) {
    std::vector<std::string> candidates;
    std::string d = domain;
    while (true) {
        candidates.push_back(d);
        auto dot = d.find('.');
        if (dot == std::string::npos) break;
        d = d.substr(dot + 1);
    }
    return candidates;
}

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct ListLookupData {
    IpSet ip_set;
    std::set<std::string> domain_set; // normalized lowercase, wildcard prefix stripped
};

class ListLookupBuilder : public ListEntryVisitor {
public:
    ListLookupData data;

    void on_entry(EntryType type, std::string_view entry) override {
        switch (type) {
            case EntryType::Ip:
                data.ip_set.add_address(std::string(entry));
                break;
            case EntryType::Cidr:
                data.ip_set.add_cidr(std::string(entry));
                break;
            case EntryType::Domain: {
                std::string d(entry);
                if (has_prefix(d, "*.")) d = d.substr(2);
                std::transform(d.begin(), d.end(), d.begin(), ::tolower);
                data.domain_set.insert(std::move(d));
                break;
            }
        }
    }
};

// Pre-build lookup data for all lists referenced in route rules.
std::map<std::string, ListLookupData> build_all_lookups(const Config& config,
                                                          const CacheManager& cache) {
    std::map<std::string, ListLookupData> result;
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});
    const auto& lists_map =
        config.lists.value_or(std::map<std::string, ListConfig>{});
    ListStreamer streamer(cache);

    std::set<std::string> referenced;
    for (const auto& rule : route_rules) {
        if (!route_rule_enabled(rule)) {
            continue;
        }
        for (const auto& list_name : route_rule_lists(rule)) {
            referenced.insert(list_name);
        }
    }

    for (const auto& list_name : referenced) {
        auto it = lists_map.find(list_name);
        if (it == lists_map.end()) continue;
        ListLookupBuilder builder;
        streamer.stream_list(list_name, it->second, builder);
        result.emplace(list_name, std::move(builder.data));
    }
    return result;
}

bool append_unique_ip(std::vector<std::string>& ips, const std::string& ip) {
    if (ip.empty() || std::find(ips.begin(), ips.end(), ip) != ips.end()) {
        return false;
    }
    ips.push_back(ip);
    return true;
}

bool extract_ips_from_dns_answer(const unsigned char* answer,
                                 int answer_len,
                                 int expected_type,
                                 std::vector<std::string>& ips,
                                 std::string* error_out) {
    ns_msg handle {};
    if (ns_initparse(answer, answer_len, &handle) < 0) {
        if (error_out) {
            *error_out = "Failed to parse DNS response";
        }
        return false;
    }

    const int answer_count = ns_msg_count(handle, ns_s_an);
    bool found = false;
    for (int i = 0; i < answer_count; ++i) {
        ns_rr rr {};
        if (ns_parserr(&handle, ns_s_an, i, &rr) < 0) {
            continue;
        }
        if (ns_rr_class(rr) != ns_c_in || ns_rr_type(rr) != expected_type) {
            continue;
        }

        char buf[INET6_ADDRSTRLEN] = {};
        if (expected_type == ns_t_a && ns_rr_rdlen(rr) == 4) {
            if (inet_ntop(AF_INET, ns_rr_rdata(rr), buf, sizeof(buf)) != nullptr) {
                found = append_unique_ip(ips, buf) || found;
            }
        } else if (expected_type == ns_t_aaaa && ns_rr_rdlen(rr) == 16) {
            if (inet_ntop(AF_INET6, ns_rr_rdata(rr), buf, sizeof(buf)) != nullptr) {
                found = append_unique_ip(ips, buf) || found;
            }
        }
    }

    return found;
}

std::optional<std::string> query_dns_record_with_resolver(
    const std::optional<DnsServerConfig>& server,
    const std::string& domain,
    int record_type,
    std::vector<std::string>& ips) {
    std::array<unsigned char, NS_PACKETSZ * 8> answer {};
    int response_len = -1;

    if (!server.has_value()) {
        response_len = res_query(domain.c_str(),
                                 ns_c_in,
                                 record_type,
                                 answer.data(),
                                 static_cast<int>(answer.size()));
    } else {
        sockaddr_storage resolver_addr {};
        socklen_t resolver_addr_len = 0;
        int address_family = AF_UNSPEC;

        if (is_ipv6_address(server->resolved_ip)) {
            auto* addr6 = reinterpret_cast<sockaddr_in6*>(&resolver_addr);
            addr6->sin6_family = AF_INET6;
            addr6->sin6_port = htons(server->port);
            if (inet_pton(AF_INET6, server->resolved_ip.c_str(), &addr6->sin6_addr) != 1) {
                return keen_pbr3::format("Resolver '{}' has invalid IPv6 address", server->address);
            }
            address_family = AF_INET6;
            resolver_addr_len = sizeof(sockaddr_in6);
        } else {
            auto* addr4 = reinterpret_cast<sockaddr_in*>(&resolver_addr);
            addr4->sin_family = AF_INET;
            addr4->sin_port = htons(server->port);
            if (inet_pton(AF_INET, server->resolved_ip.c_str(), &addr4->sin_addr) != 1) {
                return keen_pbr3::format("Resolver '{}' has invalid IPv4 address", server->address);
            }
            address_family = AF_INET;
            resolver_addr_len = sizeof(sockaddr_in);
        }

        std::array<unsigned char, NS_PACKETSZ * 2> query {};
        const int query_len = res_mkquery(ns_o_query,
                                          domain.c_str(),
                                          ns_c_in,
                                          record_type,
                                          nullptr,
                                          0,
                                          nullptr,
                                          query.data(),
                                          static_cast<int>(query.size()));
        if (query_len < 0) {
            return keen_pbr3::format("Failed to build DNS {} query",
                                     record_type == ns_t_a ? "A" : "AAAA");
        }

        const int socket_fd = socket(address_family, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            return keen_pbr3::format("Failed to create DNS socket: {}", std::strerror(errno));
        }
        const auto close_socket = [socket_fd]() { close(socket_fd); };

        timeval socket_timeout {};
        socket_timeout.tv_sec = 2;
        socket_timeout.tv_usec = 0;
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
            const std::string error = std::strerror(errno);
            close_socket();
            return keen_pbr3::format("Failed to configure DNS socket timeout: {}", error);
        }

        const ssize_t sent = sendto(socket_fd,
                                    query.data(),
                                    static_cast<size_t>(query_len),
                                    0,
                                    reinterpret_cast<const sockaddr*>(&resolver_addr),
                                    resolver_addr_len);
        if (sent != static_cast<ssize_t>(query_len)) {
            const std::string error = std::strerror(errno);
            close_socket();
            return keen_pbr3::format("Failed to send DNS {} query via '{}': {}",
                                     record_type == ns_t_a ? "A" : "AAAA",
                                     server->address,
                                     error);
        }

        const ssize_t received = recvfrom(socket_fd,
                                          answer.data(),
                                          answer.size(),
                                          0,
                                          nullptr,
                                          nullptr);
        if (received <= 0) {
            const std::string error = std::strerror(errno);
            close_socket();
            return keen_pbr3::format("DNS {} query via '{}' failed: {}",
                                     record_type == ns_t_a ? "A" : "AAAA",
                                     server->address,
                                     error);
        }
        response_len = static_cast<int>(received);
        close_socket();
    }

    if (response_len < 0) {
        const char* reason = hstrerror(h_errno);
        return keen_pbr3::format("DNS {} query via '{}' failed: {}",
                                 record_type == ns_t_a ? "A" : "AAAA",
                                 server.has_value() ? server->address : "resolv.conf",
                                 reason != nullptr ? reason : "unknown resolver error");
    }

    std::string parse_error;
    if (!extract_ips_from_dns_answer(answer.data(), response_len, record_type, ips, &parse_error) &&
        !parse_error.empty()) {
        return keen_pbr3::format("DNS {} query via '{}' failed: {}",
                                 record_type == ns_t_a ? "A" : "AAAA",
                                 server.has_value() ? server->address : "resolv.conf",
                                 parse_error);
    }

    return std::nullopt;
}

std::vector<std::string> resolve_domain_with_system_resolver(const Config& config,
                                                             const std::string& domain,
                                                             std::vector<std::string>& warnings) {
    std::vector<std::string> ips;

    const DnsConfig dns_config = config.dns.value_or(DnsConfig{});
    std::optional<DnsServerConfig> resolver;
    if (dns_config.system_resolver.has_value() &&
        !dns_config.system_resolver->address.empty()) {
        try {
            resolver = parse_dns_server("system_resolver",
                                        dns_config.system_resolver->address,
                                        std::nullopt);
        } catch (const std::exception& e) {
            warnings.push_back(keen_pbr3::format("DNS system resolver '{}' is invalid: {}",
                                                 dns_config.system_resolver->address,
                                                 e.what()));
            return ips;
        }
    }

    std::optional<std::string> a_error =
        query_dns_record_with_resolver(resolver, domain, ns_t_a, ips);
    std::optional<std::string> aaaa_error =
        query_dns_record_with_resolver(resolver, domain, ns_t_aaaa, ips);

    if (ips.empty() && a_error.has_value() && aaaa_error.has_value()) {
        warnings.push_back(keen_pbr3::format("DNS resolution failed for '{}' via system resolver '{}': {}; {}",
                                             domain,
                                             resolver.has_value() ? resolver->address : "resolv.conf",
                                             *a_error,
                                             *aaaa_error));
    }

    return ips;
}

// Walk route rules in order; return first matching outbound and match info.
std::pair<std::string, std::optional<ListMatchInfo>>
find_expected_outbound(const Config& config,
                        const std::map<std::string, ListLookupData>& lookups,
                        const std::string& ip,
                        const std::vector<std::string>& domain_cands) {
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    for (const auto& rule : route_rules) {
        if (!route_rule_enabled(rule)) {
            continue;
        }
        for (const auto& list_name : route_rule_lists(rule)) {
            auto it = lookups.find(list_name);
            if (it == lookups.end()) continue;
            const auto& lookup = it->second;

            // IP / CIDR match
            if (!ip.empty() && lookup.ip_set.contains(ip)) {
                return {rule.outbound, ListMatchInfo{list_name, ip}};
            }

            // Domain match (most-specific candidate first)
            for (const auto& candidate : domain_cands) {
                std::string lower = candidate;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (contains(lookup.domain_set, lower)) {
                    return {rule.outbound, ListMatchInfo{list_name, candidate}};
                }
            }
        }
    }

    return {"(default)", std::nullopt};
}

std::optional<ListMatchInfo> find_rule_match(const RouteRule& rule,
                                             const std::map<std::string, ListLookupData>& lookups,
                                             const std::string& ip,
                                             const std::vector<std::string>& domain_cands) {
    if (!route_rule_enabled(rule)) {
        return std::nullopt;
    }

    for (const auto& list_name : route_rule_lists(rule)) {
        auto it = lookups.find(list_name);
        if (it == lookups.end()) continue;
        const auto& lookup = it->second;

        if (!ip.empty() && lookup.ip_set.contains(ip)) {
            return ListMatchInfo{list_name, ip};
        }

        for (const auto& candidate : domain_cands) {
            std::string lower = candidate;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (contains(lookup.domain_set, lower)) {
                return ListMatchInfo{list_name, candidate};
            }
        }
    }
    return std::nullopt;
}

std::string outbound_interface_name(const Config& config, const std::string& outbound_tag) {
    const auto& outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    for (const auto& outbound : outbounds) {
        if (outbound.tag != outbound_tag) continue;
        return outbound.interface.value_or("-");
    }
    return "-";
}

std::optional<bool> test_rule_ipset_membership(const KernelSetTester& set_tester,
                                               const RuleState& rule_state,
                                               const std::string& ip,
                                               bool is_v4) {
    bool any_answer = false;

    for (const auto& set_name : rule_state.set_names) {
        const bool v4_set = has_prefix(set_name, "kpbr4_") || has_prefix(set_name, "kpbr4d_");
        const bool v6_set = has_prefix(set_name, "kpbr6_") || has_prefix(set_name, "kpbr6d_");
        if (is_v4 && !v4_set) continue;
        if (!is_v4 && !v6_set) continue;

        auto result = set_tester.contains(set_name, ip);
        if (!result.has_value()) {
            continue;
        }
        any_answer = true;
        if (*result) return true;
    }

    if (!any_answer) return std::nullopt;
    return false;
}

std::string find_actual_outbound(const KernelSetTester& set_tester,
                                   const std::vector<RuleState>& rule_states,
                                   const std::string& ip,
                                   bool is_v4) {
    bool any_answer = false;

    for (const auto& rs : rule_states) {
        if (rs.action_type == RuleActionType::Skip) continue;

        for (const auto& set_name : rs.set_names) {
            bool v4_set = has_prefix(set_name, "kpbr4_") || has_prefix(set_name, "kpbr4d_");
            bool v6_set = has_prefix(set_name, "kpbr6_") || has_prefix(set_name, "kpbr6d_");

            if (is_v4 && !v4_set) continue;
            if (!is_v4 && !v6_set) continue;

            auto result = set_tester.contains(set_name, ip);
            if (result.has_value()) {
                any_answer = true;
                if (*result) return rs.outbound_tag;
            }
        }
    }

    return any_answer ? "(default)" : "(unknown)";
}

} // namespace

TestRoutingResult compute_test_routing(const Config& config,
                                        const CacheManager& cache,
                                        const std::string& target) {
    TestRoutingResult result;
    result.target = target;
    result.is_domain = !is_ip_address(target);

    const auto lookups = build_all_lookups(config, cache);

    std::vector<std::string> ips;
    std::vector<std::string> domain_cands;

    if (result.is_domain) {
        domain_cands = domain_candidates(lowercase_copy(target));
        ips = resolve_domain_with_system_resolver(config, target, result.warnings);
        if (ips.empty() && !result.warnings.empty()) {
            result.dns_error = result.warnings.front();
        }
        result.resolved_ips = ips;
    } else {
        ips.push_back(target);
    }

    const auto marks = allocate_outbound_marks(
        config.fwmark.value_or(FwmarkConfig{}),
        config.outbounds.value_or(std::vector<Outbound>{}));
    const auto rule_states = build_fw_rule_states(config, marks);
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    std::optional<KernelSetTester> set_tester;
    try {
        set_tester.emplace(resolve_firewall_backend(firewall_backend_preference(config)));
    } catch (const std::exception& e) {
        result.warnings.push_back(
            keen_pbr3::format("Cannot check actual outbound (firewall tool unavailable): {}", e.what()));
    }

    // If DNS failed we still want to show a domain-only match row
    if (ips.empty() && result.is_domain) {
        TestRoutingEntry entry;
        entry.ip = "(no IPs resolved)";
        auto [expected, match] = find_expected_outbound(config, lookups, "", domain_cands);
        entry.expected_outbound = expected;
        entry.list_match = match;
        entry.actual_outbound = "(unknown)";
        entry.ok = false;
        result.entries.push_back(std::move(entry));
    }

    for (size_t idx = 0; idx < route_rules.size(); ++idx) {
        RuleDiagnostic diag;
        diag.rule_index = static_cast<int>(idx);
        diag.outbound = route_rules[idx].outbound;
        diag.interface_name = outbound_interface_name(config, diag.outbound);
        diag.target_match = find_rule_match(route_rules[idx], lookups,
                                            result.is_domain ? "" : target, domain_cands);
        diag.target_in_lists = diag.target_match.has_value();
        result.rule_diagnostics.push_back(std::move(diag));
    }

    for (const auto& ip : ips) {
        TestRoutingEntry entry;
        entry.ip = ip;
        auto [expected, match] = find_expected_outbound(config, lookups, ip, domain_cands);
        entry.expected_outbound = expected;
        entry.list_match = match;
        entry.actual_outbound = set_tester.has_value()
            ? find_actual_outbound(*set_tester, rule_states, ip, is_ipv4_address(ip))
            : "(unknown)";
        entry.ok = (entry.expected_outbound == entry.actual_outbound);
        result.entries.push_back(std::move(entry));

        for (size_t idx = 0; idx < result.rule_diagnostics.size(); ++idx) {
            RuleIpDiagnostic ip_diag;
            ip_diag.ip = ip;
            if (set_tester.has_value() && idx < rule_states.size()) {
                ip_diag.in_ipset = test_rule_ipset_membership(
                    *set_tester, rule_states[idx], ip, is_ipv4_address(ip));
            }
            result.rule_diagnostics[idx].ip_rows.push_back(std::move(ip_diag));
        }
    }

    result.no_matching_rule = std::none_of(
        result.entries.begin(), result.entries.end(), [](const TestRoutingEntry& entry) {
            return entry.expected_outbound != "(default)";
        });

    return result;
}

int run_test_routing_command(const Config& config,
                              const CacheManager& cache,
                              const std::string& target) {
    auto result = compute_test_routing(config, cache, target);

    for (const auto& w : result.warnings) {
        std::cerr << "Warning: " << w << "\n";
    }

    std::cout << "Target: " << result.target << "\n";
    if (!result.resolved_ips.empty()) {
        std::cout << "Resolved IPs: ";
        for (size_t i = 0; i < result.resolved_ips.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << result.resolved_ips[i];
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    constexpr int ip_w        = 25;
    constexpr int list_w      = 35;
    constexpr int outbound_w  = 18;

    std::cout << keen_pbr3::format("{:<{}} | {:<{}} | {:<{}} | {:<{}} | {}\n",
                             "IP", ip_w,
                             "List Match", list_w,
                             "Expected Outbound", outbound_w,
                             "Actual Outbound", outbound_w,
                             "Status");
    std::cout << std::string(ip_w + 3 + list_w + 3 + outbound_w + 3 + outbound_w + 3 + 6, '-')
              << "\n";

    bool all_ok = true;
    for (const auto& entry : result.entries) {
        std::string list_str = "-";
        if (entry.list_match) {
            list_str = entry.list_match->list_name;
            if (!entry.list_match->via.empty() && entry.list_match->via != entry.ip) {
                list_str += " (via " + entry.list_match->via + ")";
            }
        }

        const std::string status = entry.ok ? "OK" : "NOK";
        if (!entry.ok) all_ok = false;

        std::cout << keen_pbr3::format("{:<{}} | {:<{}} | {:<{}} | {:<{}} | {}\n",
                                 entry.ip, ip_w,
                                 list_str, list_w,
                                 entry.expected_outbound, outbound_w,
                                 entry.actual_outbound, outbound_w,
                                 status);
    }

    return all_ok ? 0 : 1;
}

} // namespace keen_pbr3
