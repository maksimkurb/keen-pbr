#include "test_routing.hpp"

#include "../config/routing_state.hpp"
#include "../config/addr_spec.hpp"
#include "../lists/domain_index.hpp"
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
#include <atomic>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <set>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
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

bool is_dynamic_set_for_ip(const std::string& set_name, bool is_v4) {
    return is_v4 ? set_name.rfind("kpbr4d_", 0) == 0
                 : set_name.rfind("kpbr6d_", 0) == 0;
}

SetWriteEvidence set_write_evidence_for_rule(
    const RuleState* state, const std::string& ip,
    const SetWriteEvidenceLookup& lookup) {
    SetWriteEvidence result;
    if (state == nullptr || !is_ip_address(ip)) return result;

    const bool is_v4 = is_ipv4_address(ip);
    bool tracked = false;
    std::optional<uint64_t> newest_age;
    for (const auto& set_name : state->set_names) {
        if (!is_dynamic_set_for_ip(set_name, is_v4)) continue;
        tracked = true;
        const SetWriteEvidence evidence = lookup(set_name, ip);
        if (evidence.status == SetWriteEvidenceStatus::Recorded) {
            result.status = SetWriteEvidenceStatus::Recorded;
            if (evidence.age_seconds.has_value() &&
                (!newest_age.has_value() || *evidence.age_seconds < *newest_age)) {
                newest_age = evidence.age_seconds;
            }
        } else if (evidence.status == SetWriteEvidenceStatus::NoRecord &&
                   result.status != SetWriteEvidenceStatus::Recorded) {
            result.status = SetWriteEvidenceStatus::NoRecord;
        }
    }
    if (!tracked) return result;
    result.age_seconds = newest_age;
    return result;
}

bool packet_family_matches_target(const TestRoutingCriteria& packet,
                                  const std::string& target_ip) {
    if (!packet.src_addr.has_value() || !is_ip_address(target_ip) ||
        !is_ip_address(*packet.src_addr)) {
        return true;
    }
    return is_ipv4_address(*packet.src_addr) == is_ipv4_address(target_ip);
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

// Lookup structures for the lists referenced by enabled route rules: IP/CIDR
// sets per list plus one DomainIndex shared by all of them (the same matcher
// the interception daemon uses).
struct ListLookupData {
    std::map<std::string, IpSet> ip_sets;
    std::map<std::string, DomainIndex::ListId> list_ids;
    DomainIndex domain_index;

    bool has_list(const std::string& name) const {
        return ip_sets.find(name) != ip_sets.end();
    }

    // The broadest list entry (shortest matching suffix) that matches one of
    // `domain_cands` (most specific candidate first).  DomainIndex answers
    // "does any suffix match", which is monotone along the candidates.
    std::optional<std::string> domain_match(const std::string& list_name,
                                            const std::vector<std::string>& domain_cands) const {
        const auto id_it = list_ids.find(list_name);
        if (id_it == list_ids.end()) return std::nullopt;
        std::optional<std::string> via;
        std::vector<DomainIndex::ListId> ids;
        for (const auto& candidate : domain_cands) {
            domain_index.lookup(candidate, ids);
            if (!std::binary_search(ids.begin(), ids.end(), id_it->second)) break;
            via = candidate;
        }
        return via;
    }
};

class ListLookupBuilder : public ListEntryVisitor {
public:
    ListLookupBuilder(IpSet& ip_set, DomainIndex::Builder& domains, DomainIndex::ListId id)
        : ip_set_(ip_set), domains_(domains), id_(id) {}

    void on_entry(EntryType type, std::string_view entry) override {
        switch (type) {
            case EntryType::Ip:
                ip_set_.add_address(std::string(entry));
                break;
            case EntryType::Cidr:
                ip_set_.add_cidr(std::string(entry));
                break;
            case EntryType::Domain:
                domains_.add_domain(id_, entry);  // lowercases, strips "*."
                break;
        }
    }

private:
    IpSet& ip_set_;
    DomainIndex::Builder& domains_;
    DomainIndex::ListId id_;
};

// Pre-build lookup data for all lists referenced in route rules.
ListLookupData build_all_lookups(const Config& config, const CacheManager& cache) {
    ListLookupData result;
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

    DomainIndex::Builder domains;
    for (const auto& list_name : referenced) {
        auto it = lists_map.find(list_name);
        if (it == lists_map.end()) continue;
        const DomainIndex::ListId id = domains.add_list(list_name);
        result.list_ids.emplace(list_name, id);
        ListLookupBuilder builder(result.ip_sets[list_name], domains, id);
        streamer.stream_list(list_name, it->second, builder);
    }
    result.domain_index = std::move(domains).build();
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

// The legacy BIND resolver routines operate on the process-global `_res` state
// and are not thread-safe; test-routing requests may run on several API worker
// threads, so serialize them (the res_n* variants are absent on musl).
std::mutex& legacy_resolver_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<std::string> query_dns_record_with_system_resolver(
    const std::string& domain,
    int record_type,
    std::vector<std::string>& ips) {
    std::array<unsigned char, NS_PACKETSZ * 8> answer {};
    int response_len = -1;
    {
        std::lock_guard<std::mutex> resolver_lock(legacy_resolver_mutex());
        response_len = res_query(domain.c_str(),
                                 ns_c_in,
                                 record_type,
                                 answer.data(),
                                 static_cast<int>(answer.size()));
    }

    if (response_len < 0) {
        const char* reason = hstrerror(h_errno);
        return keen_pbr3::format("DNS {} query via 'resolv.conf' failed: {}",
                                 record_type == ns_t_a ? "A" : "AAAA",
                                 reason != nullptr ? reason : "unknown resolver error");
    }

    std::string parse_error;
    if (!extract_ips_from_dns_answer(answer.data(), response_len, record_type, ips, &parse_error) &&
        !parse_error.empty()) {
        return keen_pbr3::format("DNS {} query via 'resolv.conf' failed: {}",
                                 record_type == ns_t_a ? "A" : "AAAA",
                                 parse_error);
    }

    return std::nullopt;
}

std::function<std::vector<std::string>(const std::string&)>& domain_resolver_override() {
    static std::function<std::vector<std::string>(const std::string&)> override_fn;
    return override_fn;
}

std::vector<std::string> resolve_domain_with_system_resolver(const std::string& domain,
                                                             std::vector<std::string>& warnings) {
    if (domain_resolver_override()) {
        return domain_resolver_override()(domain);
    }
    std::vector<std::string> ips;

    std::optional<std::string> a_error =
        query_dns_record_with_system_resolver(domain, ns_t_a, ips);
    std::optional<std::string> aaaa_error =
        query_dns_record_with_system_resolver(domain, ns_t_aaaa, ips);

    if (ips.empty() && a_error.has_value() && aaaa_error.has_value()) {
        warnings.push_back(keen_pbr3::format("DNS resolution failed for '{}' via system resolver: {}; {}",
                                             domain,
                                             *a_error,
                                             *aaaa_error));
    }

    return ips;
}

// Walk route rules in order; return first matching outbound and match info.
std::optional<bool> combine_match(std::optional<bool> current,
                                  std::optional<bool> next) {
    if (current.has_value() && !*current) return false;
    if (next.has_value() && !*next) return false;
    if (!current.has_value() || !next.has_value()) return std::nullopt;
    return true;
}

bool ip_matches_spec(const std::vector<std::string>& specs, const std::string& ip) {
    if (ip.empty()) return false;
    IpSet set;
    for (const auto& spec : specs) {
        if (spec.find('/') == std::string::npos) {
            set.add_address(spec);
        } else {
            set.add_cidr(spec);
        }
    }
    return set.contains(ip);
}

bool address_spec_applies_to_ip(const std::vector<std::string>& specs,
                                const std::string& ip) {
    if (ip.empty()) return false;
    const bool ip_v4 = is_ipv4_address(ip);
    bool has_v4 = false;
    bool has_v6 = false;
    for (const auto& spec : specs) {
        const std::string address = spec.substr(0, spec.find('/'));
        if (address.find(':') == std::string::npos) {
            has_v4 = true;
        } else {
            has_v6 = true;
        }
    }
    return (ip_v4 && has_v4) || (!ip_v4 && has_v6) || (has_v4 && has_v6);
}

bool port_matches_spec(const PortSpec& spec, uint16_t port) {
    return std::any_of(spec.ranges.begin(), spec.ranges.end(),
                       [port](const PortRange& range) {
                           return port >= range.from && port <= range.to;
                       });
}

std::optional<bool> match_rule_criteria(const RouteRule& rule,
                                        const std::string& ip,
                                        const TestRoutingCriteria& packet) {
    if (!packet.has_any()) return std::nullopt;
    if (!route_rule_enabled(rule)) return false;

    const FirewallRuleCriteria criteria = build_firewall_rule_criteria(rule);
    std::optional<bool> result = true;

    if (criteria.default_gateway != DefaultGatewayFamily::None) {
        const bool packet_v4 = !ip.empty() && is_ipv4_address(ip);
        const bool gateway_v4 = criteria.default_gateway == DefaultGatewayFamily::Ipv4;
        if (!ip.empty() && packet_v4 != gateway_v4) {
            return false;
        }
        // Directness depends on the current main-table route and cannot be
        // established from a synthetic packet alone.
        result = combine_match(result, std::nullopt);
    }

    if (criteria.proto != L4Proto::Any || !criteria.src_port.empty() ||
        !criteria.dst_port.empty()) {
        if (!packet.proto.has_value()) {
            result = combine_match(result, std::nullopt);
        } else {
            const bool transport = *packet.proto == "tcp" || *packet.proto == "udp";
            bool proto_match = true;
            switch (criteria.proto) {
            case L4Proto::Any:
                proto_match = transport || (criteria.src_port.empty() && criteria.dst_port.empty());
                break;
            case L4Proto::Tcp:
                proto_match = *packet.proto == "tcp";
                break;
            case L4Proto::Udp:
                proto_match = *packet.proto == "udp";
                break;
            case L4Proto::TcpUdp:
                proto_match = transport;
                break;
            }
            result = combine_match(result, proto_match);
        }
    }

    const auto match_port = [&](const PortSpec& spec,
                                bool negate,
                                const std::optional<uint16_t>& port) -> std::optional<bool> {
        if (spec.empty()) return std::optional<bool>{true};
        if (!packet.proto.has_value() || *packet.proto == "other") {
            return packet.proto.has_value() ? std::optional<bool>{false} : std::nullopt;
        }
        if (!port.has_value()) return std::optional<bool>{};
        const bool matched = port_matches_spec(spec, *port);
        return std::optional<bool>{negate ? !matched : matched};
    };
    result = combine_match(result, match_port(criteria.src_port,
                                               criteria.negate_src_port,
                                               packet.src_port));
    result = combine_match(result, match_port(criteria.dst_port,
                                               criteria.negate_dst_port,
                                               packet.dest_port));

    if (!criteria.src_addr.empty()) {
        const auto address_match = packet.src_addr.has_value()
            ? std::optional<bool>{
                  address_spec_applies_to_ip(criteria.src_addr, *packet.src_addr)
                      ? (criteria.negate_src_addr
                             ? !ip_matches_spec(criteria.src_addr, *packet.src_addr)
                             : ip_matches_spec(criteria.src_addr, *packet.src_addr))
                      : false}
            : std::nullopt;
        result = combine_match(result, address_match);
    }
    if (!criteria.dst_addr.empty()) {
        const auto address_match = ip.empty()
            ? std::nullopt
            : std::optional<bool>{
                  address_spec_applies_to_ip(criteria.dst_addr, ip)
                      ? (criteria.negate_dst_addr
                             ? !ip_matches_spec(criteria.dst_addr, ip)
                             : ip_matches_spec(criteria.dst_addr, ip))
                      : false};
        result = combine_match(result, address_match);
    }
    if (criteria.dscp.has_value()) {
        result = combine_match(result,
                               packet.dscp.has_value()
                                   ? std::optional<bool>{*criteria.dscp == *packet.dscp}
                                   : std::nullopt);
    }

    return result;
}

struct ExpectedRoutingMatch {
    std::string outbound{"(default)"};
    std::optional<ListMatchInfo> list_match;
    std::optional<int> rule_index;
    std::optional<bool> criteria_match;
};

ExpectedRoutingMatch find_expected_outbound(const Config& config,
                                            const ListLookupData& lookups,
                                            const std::string& ip,
                                            const std::vector<std::string>& domain_cands,
                                            const TestRoutingCriteria& packet) {
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    for (size_t index = 0; index < route_rules.size(); ++index) {
        const auto& rule = route_rules[index];
        if (!route_rule_enabled(rule)) {
            continue;
        }
        if (!packet.has_any() && route_rule_lists(rule).empty()) {
            continue;
        }
        const auto criteria_match = packet.has_any()
            ? match_rule_criteria(rule, ip, packet)
            : std::optional<bool>{true};
        if (criteria_match.has_value() && !*criteria_match) {
            continue;
        }
        for (const auto& list_name : route_rule_lists(rule)) {
            if (!lookups.has_list(list_name)) continue;

            // IP / CIDR match
            if (!ip.empty() && lookups.ip_sets.at(list_name).contains(ip)) {
                if (!criteria_match.has_value()) {
                    return {"(unknown)", ListMatchInfo{list_name, ip}, std::nullopt, std::nullopt};
                }
                return {rule.outbound,
                        ListMatchInfo{list_name, ip},
                        static_cast<int>(index),
                        criteria_match};
            }

            // Domain match
            if (auto via = lookups.domain_match(list_name, domain_cands)) {
                if (!criteria_match.has_value()) {
                    return {"(unknown)", ListMatchInfo{list_name, *via}, std::nullopt, std::nullopt};
                }
                return {rule.outbound,
                        ListMatchInfo{list_name, *via},
                        static_cast<int>(index),
                        criteria_match};
            }
        }
        if (!route_rule_lists(rule).empty()) continue;
        if (!criteria_match.has_value()) {
            return {"(unknown)", std::nullopt, std::nullopt, std::nullopt};
        }
        if (criteria_match.has_value() && *criteria_match) {
            return {rule.outbound, std::nullopt, static_cast<int>(index), criteria_match};
        }
    }

    return {};
}

std::optional<ListMatchInfo> find_rule_match(const RouteRule& rule,
                                             const ListLookupData& lookups,
                                             const std::string& ip,
                                             const std::vector<std::string>& domain_cands) {
    if (!route_rule_enabled(rule)) {
        return std::nullopt;
    }

    for (const auto& list_name : route_rule_lists(rule)) {
        if (!lookups.has_list(list_name)) continue;

        if (!ip.empty() && lookups.ip_sets.at(list_name).contains(ip)) {
            return ListMatchInfo{list_name, ip};
        }

        if (auto via = lookups.domain_match(list_name, domain_cands)) {
            return ListMatchInfo{list_name, *via};
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
    bool applicable = false;
    bool unavailable = false;

    for (const auto& set_name : rule_state.set_names) {
        const bool v4_set = has_prefix(set_name, "kpbr4_") || has_prefix(set_name, "kpbr4d_");
        const bool v6_set = has_prefix(set_name, "kpbr6_") || has_prefix(set_name, "kpbr6d_");
        if (is_v4 && !v4_set) continue;
        if (!is_v4 && !v6_set) continue;

        applicable = true;
        auto result = set_tester.contains(set_name, ip);
        if (!result.has_value()) {
            unavailable = true;
            continue;
        }
        if (*result) return true;
    }

    if (!applicable || unavailable) return std::nullopt;
    return false;
}

std::string find_actual_outbound(
    const std::vector<RuleState>& rule_states,
    const std::vector<RuleIpDiagnostic>& rule_ip_diagnostics,
    bool have_realized_rules,
    bool use_packet_criteria) {
    if (!use_packet_criteria) {
        // Preserve the target-only diagnostic's historical list-membership
        // behavior: an unavailable earlier set does not mask a later set
        // membership that can be checked.
        bool any_answer = false;
        for (const auto& rs : rule_states) {
            if (rs.action_type == RuleActionType::Skip ||
                rs.rule_index >= rule_ip_diagnostics.size() ||
                rs.list_names.empty()) {
                continue;
            }
            const auto& membership = rule_ip_diagnostics[rs.rule_index].in_ipset;
            if (!membership.has_value()) continue;
            any_answer = true;
            if (*membership) return rs.outbound_tag;
        }
        return any_answer ? "(default)" : "(unknown)";
    }

    bool any_answer = false;
    bool unknown_answer = false;
    bool known_no_match = false;

    for (const auto& rs : rule_states) {
        if (rs.action_type == RuleActionType::Skip ||
            rs.rule_index >= rule_ip_diagnostics.size()) {
            continue;
        }

        const auto& diagnostic = rule_ip_diagnostics[rs.rule_index];
        if (diagnostic.criteria_match.has_value() && !*diagnostic.criteria_match) {
            known_no_match = true;
            continue;
        }
        if (!diagnostic.criteria_match.has_value() &&
            diagnostic.in_ipset.has_value()) {
            // A null criteria result can mean an omitted packet field or an
            // untestable default-gateway selector. Never infer a live hit.
            if (*diagnostic.in_ipset) {
                unknown_answer = true;
            } else {
                known_no_match = true;
            }
            continue;
        }

        if (rs.list_names.empty()) {
            if (!have_realized_rules) {
                unknown_answer = true;
            } else if (diagnostic.criteria_match.has_value() &&
                       *diagnostic.criteria_match) {
                if (!unknown_answer) return rs.outbound_tag;
            } else {
                unknown_answer = true;
            }
            continue;
        }

        const auto& membership = diagnostic.in_ipset;
        if (membership.has_value()) {
            any_answer = true;
            if (*membership) {
                if (!unknown_answer) return rs.outbound_tag;
            }
        } else {
            unknown_answer = true;
        }
    }

    if (unknown_answer) return "(unknown)";
    return any_answer || known_no_match ? "(default)" : "(unknown)";
}

// Kernel membership checks invoke nft/ipset subprocesses. Keep the per-request
// bound low because the daemon may run multiple routing tests concurrently.
constexpr std::size_t kTestRoutingMaxConcurrentIps = 2;

struct PerIpRoutingResult {
    TestRoutingEntry entry;
    std::vector<RuleIpDiagnostic> rule_ip_diagnostics;
};

} // namespace

void set_domain_resolver_for_tests(
    std::function<std::vector<std::string>(const std::string& domain)> resolver) {
    domain_resolver_override() = std::move(resolver);
}

TestRoutingResult compute_test_routing(const Config& config,
                                        const CacheManager& cache,
                                        const std::string& target,
                                        const std::vector<RuleState>* realized_rule_states) {
    return compute_test_routing(config, cache, target, TestRoutingCriteria{},
                                realized_rule_states);
}

TestRoutingResult compute_test_routing(const Config& config,
                                        const CacheManager& cache,
                                        const std::string& target,
                                        const TestRoutingCriteria& packet,
                                        const std::vector<RuleState>* realized_rule_states,
                                        SetWriteEvidenceLookup set_write_evidence_lookup) {
    TestRoutingResult result;
    result.target = target;
    result.is_domain = !is_ip_address(target);

    const auto lookups = build_all_lookups(config, cache);

    std::vector<std::string> ips;
    std::vector<std::string> domain_cands;

    if (result.is_domain) {
        domain_cands = domain_candidates(lowercase_copy(target));
        ips = resolve_domain_with_system_resolver(target, result.warnings);
        if (ips.empty() && !result.warnings.empty()) {
            result.dns_error = result.warnings.front();
        }
        result.resolved_ips = ips;
    } else {
        ips.push_back(target);
    }

    const bool have_realized_rules = realized_rule_states != nullptr;
    std::vector<RuleState> configured_rule_states;
    if (realized_rule_states == nullptr) {
        const auto marks = allocate_outbound_marks(
            config.fwmark.value_or(FwmarkConfig{}),
            config.outbounds.value_or(std::vector<Outbound>{}));
        configured_rule_states = build_fw_rule_states(config, marks);
        realized_rule_states = &configured_rule_states;
    }
    const auto& rule_states = *realized_rule_states;
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
        auto expected = find_expected_outbound(config, lookups, "", domain_cands, packet);
        entry.expected_outbound = expected.outbound;
        entry.list_match = expected.list_match;
        entry.matched_rule_index = expected.rule_index;
        entry.criteria_match = expected.criteria_match;
        if (packet.has_any() && entry.expected_outbound == "(default)") {
            entry.criteria_match = false;
        }
        if (!packet.has_any()) {
            entry.criteria_match.reset();
        }
        entry.actual_outbound = "(unknown)";
        entry.ok = false;
        result.entries.push_back(std::move(entry));
    }

    for (size_t idx = 0; idx < route_rules.size(); ++idx) {
        RuleDiagnostic diag;
        diag.rule_index = static_cast<int>(idx);
        diag.rule = route_rules[idx];
        diag.outbound = route_rules[idx].outbound;
        diag.interface_name = outbound_interface_name(config, diag.outbound);
        diag.target_match = find_rule_match(route_rules[idx], lookups,
                                            result.is_domain ? "" : target, domain_cands);
        diag.target_in_lists = diag.target_match.has_value();
        result.rule_diagnostics.push_back(std::move(diag));
    }

    std::vector<PerIpRoutingResult> per_ip_results(ips.size());
    const auto check_ip = [&](size_t ip_index) {
        const auto& ip = ips[ip_index];
        auto& per_ip = per_ip_results[ip_index];

        per_ip.entry.ip = ip;
        if (!packet_family_matches_target(packet, ip)) {
            // A packet cannot carry an IPv4 source and an IPv6 destination
            // (or vice versa). Keep this resolved row visible, but do not
            // claim a route decision for an impossible packet.
            per_ip.entry.expected_outbound = "(unknown)";
            per_ip.entry.actual_outbound = "(unknown)";
            per_ip.entry.ok = false;
            for (size_t idx = 0; idx < result.rule_diagnostics.size(); ++idx) {
                RuleIpDiagnostic ip_diag;
                ip_diag.ip = ip;
                ip_diag.list_match = find_rule_match(
                    route_rules[idx], lookups, ip, domain_cands);
                ip_diag.in_lists = ip_diag.list_match.has_value();
                ip_diag.criteria_match = std::nullopt;
                per_ip.rule_ip_diagnostics.push_back(std::move(ip_diag));
            }
            return;
        }

        auto expected = find_expected_outbound(config, lookups, ip, domain_cands, packet);
        per_ip.entry.expected_outbound = expected.outbound;
        per_ip.entry.list_match = std::move(expected.list_match);
        per_ip.entry.matched_rule_index = expected.rule_index;
        per_ip.entry.criteria_match = expected.criteria_match;
        if (packet.has_any() && per_ip.entry.expected_outbound == "(default)") {
            per_ip.entry.criteria_match = false;
        }
        if (!packet.has_any()) {
            per_ip.entry.criteria_match.reset();
        }

        per_ip.rule_ip_diagnostics.reserve(result.rule_diagnostics.size());
        for (size_t idx = 0; idx < result.rule_diagnostics.size(); ++idx) {
            RuleIpDiagnostic ip_diag;
            ip_diag.ip = ip;
            ip_diag.list_match = find_rule_match(
                route_rules[idx], lookups, ip, domain_cands);
            ip_diag.in_lists = ip_diag.list_match.has_value();
            ip_diag.criteria_match = packet.has_any()
                ? match_rule_criteria(route_rules[idx], ip, packet)
                : std::nullopt;
            const auto state_it = std::find_if(
                rule_states.begin(), rule_states.end(),
                [idx](const RuleState& state) { return state.rule_index == idx; });
            if (set_write_evidence_lookup) {
                ip_diag.set_write_evidence = set_write_evidence_for_rule(
                    state_it != rule_states.end() ? &*state_it : nullptr,
                    ip, set_write_evidence_lookup);
            }
            if (set_tester.has_value() && state_it != rule_states.end()) {
                ip_diag.in_ipset = test_rule_ipset_membership(
                    *set_tester, *state_it, ip, is_ipv4_address(ip));
            }
            per_ip.rule_ip_diagnostics.push_back(std::move(ip_diag));
        }

        // No-list rules can be resolved from the realized rule state even
        // when the firewall set utility is unavailable. List-backed rules
        // remain unknown because their in-set membership is then null.
        per_ip.entry.actual_outbound = find_actual_outbound(
            rule_states, per_ip.rule_ip_diagnostics,
            have_realized_rules, packet.has_any());
        per_ip.entry.ok = per_ip.entry.expected_outbound != "(unknown)" &&
                          per_ip.entry.expected_outbound == per_ip.entry.actual_outbound;
    };

    const size_t worker_count = std::min(kTestRoutingMaxConcurrentIps, ips.size());
    if (worker_count > 1) {
        std::atomic_size_t next_ip{0};
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&]() {
                while (true) {
                    const size_t ip_index = next_ip.fetch_add(1);
                    if (ip_index >= ips.size()) {
                        return;
                    }
                    check_ip(ip_index);
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
    } else if (worker_count == 1) {
        check_ip(0);
    }

    for (auto& per_ip : per_ip_results) {
        result.entries.push_back(std::move(per_ip.entry));
        for (size_t idx = 0; idx < result.rule_diagnostics.size(); ++idx) {
            result.rule_diagnostics[idx].ip_rows.push_back(
                std::move(per_ip.rule_ip_diagnostics[idx]));
        }
    }

    if (packet.has_any()) {
        const bool criteria_unknown = std::any_of(
            result.entries.begin(), result.entries.end(),
            [](const TestRoutingEntry& entry) {
                return entry.expected_outbound == "(unknown)" ||
                       entry.actual_outbound == "(unknown)";
            });
        if (criteria_unknown) {
            result.warnings.push_back(
                "Some route-rule packet criteria or live firewall checks could not be evaluated because the request was incomplete, address families were incompatible, directness was unknown, or firewall state was unavailable.");
        }
    }

    result.no_matching_rule = std::none_of(
        result.entries.begin(), result.entries.end(), [](const TestRoutingEntry& entry) {
            return entry.expected_outbound != "(default)";
        });

    return result;
}

namespace {
int render_test_routing_result(const TestRoutingResult& result) {
    for (const auto& w : result.warnings) {
        std::cerr << "Warning: " << w << "\n";
    }
    if (result.dns_error.has_value()) {
        std::cerr << "DNS error: " << *result.dns_error << "\n";
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

    bool all_ok = !result.dns_error.has_value();
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
} // namespace

int run_test_routing_command(const Config& config,
                              const CacheManager& cache,
                              const std::string& target) {
    return render_test_routing_result(compute_test_routing(config, cache, target));
}

int run_test_routing_command(const Config& config,
                              const CacheManager& cache,
                              const std::string& target,
                              const std::vector<RuleState>& realized_rule_states) {
    return render_test_routing_result(
        compute_test_routing(config, cache, target, &realized_rule_states));
}

int run_test_routing_command(const nlohmann::json& response) {
    const auto& payload = response.at("result");
    TestRoutingResult result;
    result.target = payload.value("target", "");
    result.resolved_ips = payload.value("resolved_ips", std::vector<std::string>{});
    result.warnings = payload.value("warnings", std::vector<std::string>{});
    if (payload.contains("dns_error") && !payload.at("dns_error").is_null()) {
        result.dns_error = payload.at("dns_error").get<std::string>();
    }
    for (const auto& item : payload.value("entries", nlohmann::json::array())) {
        TestRoutingEntry entry;
        entry.ip = item.value("ip", "");
        entry.expected_outbound = item.value("expected_outbound", "(unknown)");
        entry.actual_outbound = item.value("actual_outbound", "(unknown)");
        entry.ok = item.value("ok", false);
        if (item.contains("list_match") && item.at("list_match").is_object()) {
            entry.list_match = ListMatchInfo{
                item.at("list_match").value("list_name", ""),
                item.at("list_match").value("via", "")};
        }
        result.entries.push_back(std::move(entry));
    }
    return render_test_routing_result(result);
}

} // namespace keen_pbr3
