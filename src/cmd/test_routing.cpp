#include "test_routing.hpp"

#include "../config/routing_state.hpp"
#include "../firewall/firewall.hpp"
#include "../lists/ipset.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/format_compat.hpp"
#include "../util/string_compat.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <netdb.h>
#include <set>
#include <string>
#include <sys/socket.h>
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

std::vector<std::string> resolve_domain(const std::string& domain,
                                         std::vector<std::string>& warnings) {
    std::vector<std::string> ips;
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;

    int rc = getaddrinfo(domain.c_str(), nullptr, &hints, &result);
    if (rc != 0) {
        warnings.push_back(
            keen_pbr3::format("DNS resolution failed for '{}': {}", domain, gai_strerror(rc)));
        return ips;
    }

    for (auto* p = result; p != nullptr; p = p->ai_next) {
        char buf[INET6_ADDRSTRLEN] = {};
        if (p->ai_family == AF_INET) {
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr,
                      buf, sizeof(buf));
        } else if (p->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr,
                      buf, sizeof(buf));
        } else {
            continue;
        }
        std::string ip(buf);
        if (std::find(ips.begin(), ips.end(), ip) == ips.end()) {
            ips.push_back(ip);
        }
    }
    freeaddrinfo(result);
    return ips;
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
        for (const auto& list_name : rule.list) {
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

// Walk route rules in order; return first matching outbound and match info.
std::pair<std::string, std::optional<ListMatchInfo>>
find_expected_outbound(const Config& config,
                        const std::map<std::string, ListLookupData>& lookups,
                        const std::string& ip,
                        const std::vector<std::string>& domain_cands) {
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    for (const auto& rule : route_rules) {
        for (const auto& list_name : rule.list) {
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

std::string find_actual_outbound(const Firewall& firewall,
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

            auto result = firewall.test_ip_in_set(set_name, ip);
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

    std::vector<std::string> ips;
    std::vector<std::string> domain_cands;

    if (result.is_domain) {
        ips = resolve_domain(target, result.warnings);
        domain_cands = domain_candidates(target);
        result.resolved_ips = ips;
    } else {
        ips.push_back(target);
    }

    const auto lookups = build_all_lookups(config, cache);

    const auto marks = allocate_outbound_marks(
        config.fwmark.value_or(FwmarkConfig{}),
        config.outbounds.value_or(std::vector<Outbound>{}));
    const auto rule_states = build_fw_rule_states(config, marks);

    std::unique_ptr<Firewall> firewall;
    try {
        firewall = create_firewall("auto");
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
        return result;
    }

    for (const auto& ip : ips) {
        TestRoutingEntry entry;
        entry.ip = ip;
        auto [expected, match] = find_expected_outbound(config, lookups, ip, domain_cands);
        entry.expected_outbound = expected;
        entry.list_match = match;
        entry.actual_outbound = firewall
            ? find_actual_outbound(*firewall, rule_states, ip, is_ipv4_address(ip))
            : "(unknown)";
        entry.ok = (entry.expected_outbound == entry.actual_outbound);
        result.entries.push_back(std::move(entry));
    }

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
