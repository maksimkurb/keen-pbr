#include "test_routing.hpp"
#include "test_routing_json.hpp"
#include "test_routing_report.hpp"

#include "../config/routing_state.hpp"
#include "../dns/dns_client.hpp"
#include "../lists/domain_index.hpp"
#include "../lists/ipset.hpp"
#include "../lists/kernel_set_tester.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/format_compat.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/firewall_backend_utils.hpp"
#include "../util/string_compat.hpp"
#include "../util/term.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <set>
#include <sstream>
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

template <typename T>
void append_unique_strings(std::vector<std::string>& out, const T& items) {
    for (const auto& item : items) {
        if (std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
    }
}

std::vector<std::string> resolve_domain_for_routing_test(const std::string& domain,
                                                         const RoutingDnsResolver& resolver,
                                                         TestRoutingResult& result) {
    TestRoutingDnsInfo info;
    if (domain_resolver_override()) {
        info.resolver = "test";
        result.dns = info;
        return domain_resolver_override()(domain);
    }
    if (resolver.source == RoutingDnsSource::System) {
        info.resolver = "system";
        result.dns = info;
        auto ips = resolve_domain_with_system_resolver(domain, result.warnings);
        if (ips.empty() && !result.warnings.empty()) result.dns_error = result.warnings.front();
        return ips;
    }
    info.resolver = "dnsmasq";
    info.endpoint = (resolver.address.find(':') != std::string::npos
                         ? "[" + resolver.address + "]" : resolver.address) +
                    ":" + std::to_string(resolver.port);
    if (!resolver.instance.empty()) info.instance = resolver.instance;
    if (resolver.unavailable) {
        result.dns = info;
        result.dns_error = "DNS resolution skipped: " + *resolver.unavailable;
        result.warnings.push_back(*result.dns_error);
        return {};
    }
    if (resolver.warning) result.warnings.push_back(*resolver.warning);
    const auto timeout = std::chrono::milliseconds(2000);
    const auto a = dns_query(resolver.address, resolver.port, domain, kDnsTypeA, kDnsClassIN, timeout);
    const auto aaaa = dns_query(resolver.address, resolver.port, domain, kDnsTypeAAAA, kDnsClassIN, timeout);
    info.a_status = dns_query_status_name(a.status);
    info.aaaa_status = dns_query_status_name(aaaa.status);
    append_unique_strings(info.cname_chain, a.cname_chain);
    append_unique_strings(info.cname_chain, aaaa.cname_chain);
    std::vector<std::string> ips;
    append_unique_strings(ips, a.addresses);
    append_unique_strings(ips, aaaa.addresses);
    const std::pair<const char*, const DnsQueryResult*> queries[] = {{"A", &a}, {"AAAA", &aaaa}};
    for (const auto& [label, r] : queries) {
        if (r->status != DnsQueryStatus::Ok && r->status != DnsQueryStatus::NoData) {
            result.warnings.push_back(keen_pbr3::format(
                "{} query for '{}' via dnsmasq {} failed: {}{}", label, domain, *info.endpoint,
                dns_query_status_name(r->status), r->error.empty() ? "" : ": " + r->error));
        }
    }
    if (ips.empty()) {
        result.dns_error =
            (a.status == DnsQueryStatus::NoData && aaaa.status == DnsQueryStatus::NoData)
                ? keen_pbr3::format("dnsmasq {} returned no A/AAAA records for '{}'", *info.endpoint, domain)
                : keen_pbr3::format("DNS resolution failed for '{}' via dnsmasq {}: A {}, AAAA {}",
                                    domain, *info.endpoint, dns_query_status_name(a.status),
                                    dns_query_status_name(aaaa.status));
    }
    result.dns = info;
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

// Why a rule's packet criteria could not be evaluated for a request.
struct UndeterminedCriteria {
    std::vector<std::string> missing;  // request fields that would resolve it
    bool default_gateway{false};       // directness cannot be resolved by a field

    void add_missing(const char* field) {
        if (std::find(missing.begin(), missing.end(), field) == missing.end()) {
            missing.emplace_back(field);
        }
    }
};

// Bypass lists of a default_gateway decision (per address family) and what the
// decision found.  Without a list for the rule's family the rule stays
// undetermined.
struct DefaultGatewayEval {
    const std::vector<DefaultGatewayBypassEntry>* ipv4_entries{nullptr};
    const std::vector<DefaultGatewayBypassEntry>* ipv6_entries{nullptr};
    // Output
    bool evaluated{false};  // a default_gateway selector was decided for this address
    bool relevant{false};   // ...and the rule's other criteria did not rule it out
    std::optional<bool> uses_default_gateway;
    std::optional<DefaultGatewayBypassEntry> bypass;
};

std::vector<DefaultGatewayBypassEntry> applied_bypass_entries(const RuleState& state) {
    std::vector<DefaultGatewayBypassEntry> entries;
    entries.reserve(state.criteria.default_gateway_bypass.size());
    for (const auto& cidr : state.criteria.default_gateway_bypass) {
        entries.push_back({cidr, DefaultGatewayBypassSource::Reserved, std::nullopt});
    }
    return entries;
}

std::optional<bool> match_rule_criteria(const RouteRule& rule,
                                        const std::string& ip,
                                        const TestRoutingCriteria& packet,
                                        UndeterminedCriteria* undetermined = nullptr,
                                        DefaultGatewayEval* gateway = nullptr) {
    // Callers decide whether a request without packet fields is evaluated at
    // all (see target_only_rule_evaluable); the absent fields are only read by
    // criteria that target-only rules never carry.
    if (!route_rule_enabled(rule)) return false;

    const FirewallRuleCriteria criteria = build_firewall_rule_criteria(rule);
    std::optional<bool> result = true;
    bool gateway_bypassed = false;

    if (criteria.default_gateway != DefaultGatewayFamily::None) {
        const bool packet_v4 = !ip.empty() && is_ipv4_address(ip);
        const bool gateway_v4 = criteria.default_gateway == DefaultGatewayFamily::Ipv4;
        if (!ip.empty() && packet_v4 != gateway_v4) {
            return false;
        }
        const auto* entries = gateway == nullptr ? nullptr
            : (gateway_v4 ? gateway->ipv4_entries : gateway->ipv6_entries);
        if (entries != nullptr && !ip.empty()) {
            gateway->evaluated = true;
            gateway->bypass = find_default_gateway_bypass(*entries, ip);
            gateway->uses_default_gateway = !gateway->bypass.has_value();
            gateway_bypassed = gateway->bypass.has_value();
        } else {
            // Without the router's routes, directness cannot be established
            // from a synthetic packet alone.
            result = combine_match(result, std::nullopt);
            if (undetermined != nullptr) undetermined->default_gateway = true;
        }
    }

    if (criteria.proto != L4Proto::Any || !criteria.src_port.empty() ||
        !criteria.dst_port.empty()) {
        if (!packet.proto.has_value()) {
            result = combine_match(result, std::nullopt);
            if (undetermined != nullptr) undetermined->add_missing("proto");
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
                                const std::optional<uint16_t>& port,
                                const char* field) -> std::optional<bool> {
        if (spec.empty()) return std::optional<bool>{true};
        if (!packet.proto.has_value() || *packet.proto == "other") {
            if (!packet.proto.has_value() && undetermined != nullptr) {
                undetermined->add_missing("proto");
            }
            return packet.proto.has_value() ? std::optional<bool>{false} : std::nullopt;
        }
        if (!port.has_value()) {
            if (undetermined != nullptr) undetermined->add_missing(field);
            return std::optional<bool>{};
        }
        const bool matched = port_matches_spec(spec, *port);
        return std::optional<bool>{negate ? !matched : matched};
    };
    result = combine_match(result, match_port(criteria.src_port,
                                               criteria.negate_src_port,
                                               packet.src_port,
                                               "src_port"));
    result = combine_match(result, match_port(criteria.dst_port,
                                               criteria.negate_dst_port,
                                               packet.dest_port,
                                               "dest_port"));

    if (!criteria.src_addr.empty()) {
        const auto address_match = packet.src_addr.has_value()
            ? std::optional<bool>{
                  address_spec_applies_to_ip(criteria.src_addr, *packet.src_addr)
                      ? (criteria.negate_src_addr
                             ? !ip_matches_spec(criteria.src_addr, *packet.src_addr)
                             : ip_matches_spec(criteria.src_addr, *packet.src_addr))
                      : false}
            : std::nullopt;
        if (!packet.src_addr.has_value() && undetermined != nullptr) {
            undetermined->add_missing("src_addr");
        }
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
        // A packet without a DSCP marking carries DSCP 0.
        result = combine_match(result, *criteria.dscp == packet.dscp.value_or(0));
    }

    if (gateway != nullptr && gateway->evaluated) {
        gateway->relevant = !result.has_value() || *result;
    }
    if (gateway_bypassed) return false;
    return result;
}

// A request that carries no packet fields (target-only) skips every rule
// without lists, except a rule whose criteria depend on the destination alone:
// default_gateway and/or dest_addr, and no proto, port, source address or DSCP.
// Such a rule is decided like in packet mode.  A default_gateway rule needs the
// router's routes for its address family; without them the rule stays ignored,
// as it always was in target-only mode, rather than turning the row unknown.
bool target_only_rule_evaluable(const RouteRule& rule,
                                const std::string& ip,
                                const DefaultGatewayEval* gateway_lists) {
    if (ip.empty() || !route_rule_enabled(rule) || !route_rule_lists(rule).empty()) {
        return false;
    }
    const FirewallRuleCriteria criteria = build_firewall_rule_criteria(rule);
    if (criteria.proto != L4Proto::Any || !criteria.src_port.empty() ||
        !criteria.dst_port.empty() || !criteria.src_addr.empty() ||
        criteria.dscp.has_value()) {
        return false;
    }
    if (criteria.default_gateway == DefaultGatewayFamily::None) {
        return !criteria.dst_addr.empty();
    }
    if (gateway_lists == nullptr) return false;
    return (criteria.default_gateway == DefaultGatewayFamily::Ipv4
                ? gateway_lists->ipv4_entries
                : gateway_lists->ipv6_entries) != nullptr;
}

struct ExpectedRoutingMatch {
    std::string outbound{"(default)"};
    std::optional<ListMatchInfo> list_match;
    std::optional<int> rule_index;
    std::optional<bool> criteria_match;
    // Set when the decision stopped at a rule that could not be evaluated.
    std::optional<int> undetermined_rule_index;
    std::vector<std::string> missing_criteria;
    bool default_gateway_directness{false};
    // First default_gateway rule the walk decided for this address.
    std::optional<DefaultGatewayDecision> default_gateway;
};

std::vector<std::string> sorted_missing_criteria(const std::vector<std::string>& missing) {
    static const char* const kOrder[] = {"src_addr", "src_port", "proto", "dest_port"};
    std::vector<std::string> out;
    for (const char* field : kOrder) {
        if (std::find(missing.begin(), missing.end(), field) != missing.end()) {
            out.emplace_back(field);
        }
    }
    return out;
}

std::optional<ListMatchInfo> find_rule_match(const RouteRule& rule,
                                             const ListLookupData& lookups,
                                             const std::string& ip,
                                             const std::vector<std::string>& domain_cands);

ExpectedRoutingMatch find_expected_outbound_walk(
                                            std::optional<DefaultGatewayDecision>& gateway_decision,
                                            const Config& config,
                                            const ListLookupData& lookups,
                                            const std::string& ip,
                                            const std::vector<std::string>& domain_cands,
                                            const TestRoutingCriteria& packet,
                                            const DefaultGatewayEval* gateway_lists = nullptr) {
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    for (size_t index = 0; index < route_rules.size(); ++index) {
        const auto& rule = route_rules[index];
        if (!route_rule_enabled(rule)) {
            continue;
        }
        const bool target_only_rule =
            !packet.has_any() && target_only_rule_evaluable(rule, ip, gateway_lists);
        if (!packet.has_any() && route_rule_lists(rule).empty() && !target_only_rule) {
            continue;
        }
        UndeterminedCriteria undetermined;
        DefaultGatewayEval gateway;
        if (gateway_lists != nullptr) {
            gateway.ipv4_entries = gateway_lists->ipv4_entries;
            gateway.ipv6_entries = gateway_lists->ipv6_entries;
        }
        const auto criteria_match = (packet.has_any() || target_only_rule)
            ? match_rule_criteria(rule, ip, packet, &undetermined, &gateway)
            : std::optional<bool>{true};
        if (gateway.relevant && !gateway_decision.has_value() &&
            (route_rule_lists(rule).empty() ||
             find_rule_match(rule, lookups, ip, domain_cands).has_value())) {
            DefaultGatewayDecision decision;
            decision.rule_index = static_cast<int>(index);
            decision.uses_default_gateway = gateway.uses_default_gateway;
            decision.bypass = gateway.bypass;
            gateway_decision = std::move(decision);
        }
        if (criteria_match.has_value() && !*criteria_match) {
            continue;
        }
        const auto unknown_result = [&](std::optional<ListMatchInfo> list_match) {
            ExpectedRoutingMatch unknown{"(unknown)", std::move(list_match),
                                         std::nullopt, std::nullopt};
            unknown.undetermined_rule_index = static_cast<int>(index);
            unknown.missing_criteria = sorted_missing_criteria(undetermined.missing);
            unknown.default_gateway_directness =
                unknown.missing_criteria.empty() && undetermined.default_gateway;
            return unknown;
        };
        for (const auto& list_name : route_rule_lists(rule)) {
            if (!lookups.has_list(list_name)) continue;

            // IP / CIDR match
            if (!ip.empty() && lookups.ip_sets.at(list_name).contains(ip)) {
                if (!criteria_match.has_value()) {
                    return unknown_result(ListMatchInfo{list_name, ip});
                }
                return {rule.outbound,
                        ListMatchInfo{list_name, ip},
                        static_cast<int>(index),
                        criteria_match};
            }

            // Domain match
            if (auto via = lookups.domain_match(list_name, domain_cands)) {
                if (!criteria_match.has_value()) {
                    return unknown_result(ListMatchInfo{list_name, *via});
                }
                return {rule.outbound,
                        ListMatchInfo{list_name, *via},
                        static_cast<int>(index),
                        criteria_match};
            }
        }
        if (!route_rule_lists(rule).empty()) continue;
        if (!criteria_match.has_value()) {
            return unknown_result(std::nullopt);
        }
        if (criteria_match.has_value() && *criteria_match) {
            return {rule.outbound, std::nullopt, static_cast<int>(index), criteria_match};
        }
    }

    return {};
}

ExpectedRoutingMatch find_expected_outbound(const Config& config,
                                            const ListLookupData& lookups,
                                            const std::string& ip,
                                            const std::vector<std::string>& domain_cands,
                                            const TestRoutingCriteria& packet,
                                            const DefaultGatewayEval* gateway_lists = nullptr) {
    std::optional<DefaultGatewayDecision> decision;
    auto match = find_expected_outbound_walk(decision, config, lookups, ip,
                                             domain_cands, packet, gateway_lists);
    match.default_gateway = std::move(decision);
    return match;
}

void apply_undetermined(TestRoutingEntry& entry, const ExpectedRoutingMatch& expected) {
    entry.undetermined_rule_index = expected.undetermined_rule_index;
    entry.missing_criteria = expected.missing_criteria;
    entry.default_gateway_directness = expected.default_gateway_directness;
    entry.default_gateway = expected.default_gateway;
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

// Criteria results for the actual side, keyed by rule index, where they differ
// from the expected side (default_gateway rules judged by the applied bypass
// list).
using ActualCriteriaOverride = std::map<size_t, std::optional<bool>>;

std::string find_actual_outbound(
    const std::vector<RuleState>& rule_states,
    const std::vector<RuleIpDiagnostic>& rule_ip_diagnostics,
    bool have_realized_rules,
    bool use_packet_criteria,
    const ActualCriteriaOverride& criteria_override = {}) {
    if (!use_packet_criteria) {
        // Preserve the target-only diagnostic's historical list-membership
        // behavior: an unavailable earlier set does not mask a later set
        // membership that can be checked.  List-less rules appear in the
        // override only when the expected side evaluates them too
        // (destination-only rules, see target_only_rule_evaluable).
        bool any_answer = false;
        bool unknown_answer = false;
        for (const auto& rs : rule_states) {
            if (rs.action_type == RuleActionType::Skip ||
                rs.rule_index >= rule_ip_diagnostics.size()) {
                continue;
            }
            if (rs.list_names.empty()) {
                const auto override_it = criteria_override.find(rs.rule_index);
                if (override_it == criteria_override.end()) continue;
                if (!override_it->second.has_value()) {
                    unknown_answer = true;
                } else if (*override_it->second) {
                    if (!unknown_answer) return rs.outbound_tag;
                } else {
                    any_answer = true;
                }
                continue;
            }
            const auto& membership = rule_ip_diagnostics[rs.rule_index].in_ipset;
            if (!membership.has_value()) continue;
            any_answer = true;
            if (*membership && !unknown_answer) return rs.outbound_tag;
        }
        if (unknown_answer) return "(unknown)";
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
        std::optional<bool> criteria_match = diagnostic.criteria_match;
        if (const auto override_it = criteria_override.find(rs.rule_index);
            override_it != criteria_override.end()) {
            criteria_match = override_it->second;
        }
        if (criteria_match.has_value() && !*criteria_match) {
            known_no_match = true;
            continue;
        }
        if (!criteria_match.has_value() &&
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
            if (have_realized_rules && criteria_match.has_value() && *criteria_match) {
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

const Outbound* find_config_outbound(const Config& config, const std::string& tag) {
    if (!config.outbounds.has_value()) return nullptr;
    const auto& outbounds = *config.outbounds;
    const auto it = std::find_if(outbounds.begin(), outbounds.end(),
                                 [&tag](const Outbound& outbound) { return outbound.tag == tag; });
    return it == outbounds.end() ? nullptr : &*it;
}

// Largest-remainder split of 100 over `weights`; the shares always sum to 100.
std::vector<uint32_t> split_shares(const std::vector<uint32_t>& weights) {
    std::vector<uint32_t> shares(weights.size(), 0);
    uint64_t total = 0;
    for (const auto weight : weights) total += weight;
    if (total == 0) return shares;
    std::vector<std::pair<uint64_t, size_t>> remainders;
    uint32_t assigned = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        shares[i] = static_cast<uint32_t>(weights[i] * 100ULL / total);
        assigned += shares[i];
        remainders.emplace_back(weights[i] * 100ULL % total, i);
    }
    std::stable_sort(remainders.begin(), remainders.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; assigned < 100 && i < remainders.size(); ++i, ++assigned) {
        ++shares[remainders[i].second];
    }
    return shares;
}

// `rule_state` is the applied rule serving the traffic (balance candidates).
std::optional<OutboundGroupBreakdown> build_outbound_group(
    const Config& config,
    const std::string& tag,
    const TestRoutingGroupState& group_state,
    const RuleState* rule_state,
    bool have_realized_rules,
    bool ipv4) {
    const Outbound* group = find_config_outbound(config, tag);
    if (group == nullptr ||
        (group->type != OutboundType::URLTEST && group->type != OutboundType::ICMPTEST)) {
        return std::nullopt;
    }
    const bool balance = outbound_uses_balance(*group);
    if (balance && (!have_realized_rules || rule_state == nullptr)) return std::nullopt;

    OutboundGroupBreakdown breakdown;
    breakdown.tag = tag;
    breakdown.strategy = balance ? "balance" : "priority";

    std::string selected;
    if (!balance) {
        const auto it = group_state.urltest_selections.find(tag);
        if (it != group_state.urltest_selections.end()) selected = it->second;
    }

    std::vector<uint32_t> in_use_weights;
    std::vector<size_t> in_use_members;
    for (const auto& tier : group->outbound_groups.value_or(std::vector<OutboundGroup>{})) {
        for (const auto& member_tag : outbound_group_tags(tier)) {
            OutboundGroupMember member;
            member.tag = member_tag;
            if (const Outbound* child = find_config_outbound(config, member_tag);
                child != nullptr && child->interface.has_value()) {
                member.interface_name = *child->interface;
            }
            if (balance) {
                member.weight = outbound_group_balance_weight(tier, member_tag);
                const auto mark = group_state.outbound_marks.find(member_tag);
                if (mark != group_state.outbound_marks.end()) {
                    member.in_use = std::any_of(
                        rule_state->balance_candidates.begin(),
                        rule_state->balance_candidates.end(),
                        [&](const FirewallBalanceCandidate& candidate) {
                            return candidate.fwmark == mark->second &&
                                   (ipv4 ? candidate.ipv4 : candidate.ipv6);
                        });
                }
                if (member.in_use) {
                    in_use_members.push_back(breakdown.members.size());
                    in_use_weights.push_back(*member.weight);
                }
                member.share_percent = 0;
            } else {
                member.in_use = !selected.empty() && member_tag == selected;
            }
            breakdown.members.push_back(std::move(member));
        }
    }
    if (balance) {
        const auto shares = split_shares(in_use_weights);
        for (size_t i = 0; i < in_use_members.size(); ++i) {
            breakdown.members[in_use_members[i]].share_percent = shares[i];
        }
    }
    return breakdown;
}

// Kernel membership checks invoke nft/ipset subprocesses. Keep the per-request
// bound low because the daemon may run multiple routing tests concurrently.
constexpr std::size_t kTestRoutingMaxConcurrentIps = 2;

struct PerIpRoutingResult {
    TestRoutingEntry entry;
    std::vector<RuleIpDiagnostic> rule_ip_diagnostics;
};

} // namespace

const char* routing_diagnosis_name(RoutingDiagnosis diagnosis) {
    switch (diagnosis) {
    case RoutingDiagnosis::Dns: return "dns";
    case RoutingDiagnosis::Criteria: return "criteria";
    case RoutingDiagnosis::NetworkState: return "network_state";
    case RoutingDiagnosis::MissingIpset: return "missing_ipset";
    case RoutingDiagnosis::Firewall: return "firewall";
    case RoutingDiagnosis::Ok: return "ok";
    case RoutingDiagnosis::OtherIpset: return "other_ipset";
    case RoutingDiagnosis::ConflictingIpsets: return "conflicting_ipsets";
    case RoutingDiagnosis::StaleIpset: return "stale_ipset";
    case RoutingDiagnosis::Mismatch: return "mismatch";
    }
    return "mismatch";
}

namespace {

const RuleIpDiagnostic* find_ip_row(const RuleDiagnostic& rule, const std::string& ip) {
    const auto it = std::find_if(rule.ip_rows.begin(), rule.ip_rows.end(),
                                 [&](const RuleIpDiagnostic& row) { return row.ip == ip; });
    return it == rule.ip_rows.end() ? nullptr : &*it;
}

} // namespace

// The rule the entry's expected outbound came from.
const RuleDiagnostic* find_expected_rule(const TestRoutingResult& result,
                                         const TestRoutingEntry& entry) {
    if (entry.matched_rule_index.has_value()) {
        for (const auto& rule : result.rule_diagnostics) {
            if (rule.rule_index == *entry.matched_rule_index) return &rule;
        }
        return nullptr;
    }
    const RuleDiagnostic* first = nullptr;
    for (const auto& rule : result.rule_diagnostics) {
        if (!route_rule_enabled(rule.rule)) continue;
        const RuleIpDiagnostic* row = find_ip_row(rule, entry.ip);
        if (row == nullptr || !row->in_lists || row->criteria_match == false) continue;
        if (first == nullptr || rule.rule_index < first->rule_index) first = &rule;
    }
    return first != nullptr && first->outbound == entry.expected_outbound ? first : nullptr;
}

RoutingDiagnosis diagnose_routing_entry(const TestRoutingResult& result,
                                        const TestRoutingEntry& entry) {
    if (result.is_domain && result.resolved_ips.empty()) return RoutingDiagnosis::Dns;
    if (entry.expected_outbound == "(unknown)") {
        // The routes could not be read: a state of the router, not a gap in
        // the request.
        return entry.default_gateway_directness ? RoutingDiagnosis::NetworkState
                                                : RoutingDiagnosis::Criteria;
    }

    const RuleDiagnostic* rule = find_expected_rule(result, entry);
    const RuleIpDiagnostic* row = rule != nullptr ? find_ip_row(*rule, entry.ip) : nullptr;

    std::vector<const RuleDiagnostic*> other_set_rules;
    for (const auto& item : result.rule_diagnostics) {
        if (!route_rule_enabled(item.rule) || route_rule_lists(item.rule).empty() ||
            (rule != nullptr && item.rule_index == rule->rule_index) ||
            item.outbound != entry.actual_outbound) {
            continue;
        }
        const RuleIpDiagnostic* item_row = find_ip_row(item, entry.ip);
        if (item_row != nullptr && item_row->in_ipset == true &&
            item_row->criteria_match != false) {
            other_set_rules.push_back(&item);
        }
    }

    // "Not in the set" is a definite answer even when the actual outbound is
    // unknown: report the missing set, not a failed check.
    const bool in_lists_not_in_set =
        row != nullptr && row->in_lists && row->in_ipset == false;
    if (entry.actual_outbound == "(unknown)") {
        return in_lists_not_in_set ? RoutingDiagnosis::MissingIpset
                                   : RoutingDiagnosis::Firewall;
    }
    if (entry.ok) return RoutingDiagnosis::Ok;
    if (in_lists_not_in_set) {
        return other_set_rules.empty() ? RoutingDiagnosis::MissingIpset
                                       : RoutingDiagnosis::OtherIpset;
    }
    if (row != nullptr && row->in_ipset == true && !other_set_rules.empty()) {
        return RoutingDiagnosis::ConflictingIpsets;
    }
    if (entry.expected_outbound == "(default)") {
        for (const RuleDiagnostic* other : other_set_rules) {
            const RuleIpDiagnostic* other_row = find_ip_row(*other, entry.ip);
            if (other_row != nullptr && !other_row->in_lists) return RoutingDiagnosis::StaleIpset;
        }
    }
    return RoutingDiagnosis::Mismatch;
}

RoutingDnsResolver default_routing_dns_resolver(const Config& config) {
    RoutingDnsResolver r;
    if (effective_resolver_integration(config) != ResolverIntegrationMode::DNSMASQ) return r;
    r.source = RoutingDnsSource::Dnsmasq;
    r.unavailable = "dnsmasq integration is enabled, but the daemon did not report a usable dnsmasq DNS endpoint";
    return r;
}

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
                                        SetWriteEvidenceLookup set_write_evidence_lookup,
                                        const RoutingDnsResolver* dns_resolver,
                                        std::optional<bool> ipv6_enabled,
                                        const TestRoutingNetworkSnapshot* network,
                                        const TestRoutingGroupState* group_state) {
    TestRoutingResult result;
    result.target = target;
    result.is_domain = !is_ip_address(target);
    result.ipv6_enabled = ipv6_enabled.value_or(resolve_ipv6_support(config).enabled);

    const auto lookups = build_all_lookups(config, cache);

    std::vector<std::string> ips;
    std::vector<std::string> domain_cands;

    if (result.is_domain) {
        domain_cands = domain_candidates(lowercase_copy(target));
        const RoutingDnsResolver effective =
            dns_resolver != nullptr ? *dns_resolver : default_routing_dns_resolver(config);
        ips = resolve_domain_for_routing_test(target, effective, result);
        result.resolved_ips = ips;
    } else {
        ips.push_back(target);
    }

    // With IPv6 off there are no IPv6 sets to look at, so those addresses are
    // listed in resolved_ips but never evaluated.
    std::vector<std::string> eval_ips;
    eval_ips.reserve(ips.size());
    for (const auto& ip : ips) {
        if (result.ipv6_enabled || is_ipv4_address(ip)) eval_ips.push_back(ip);
    }

    const bool have_realized_rules = realized_rule_states != nullptr;
    std::vector<RuleState> configured_rule_states;
    if (realized_rule_states == nullptr) {
        const auto marks = allocate_outbound_marks(config);
        configured_rule_states = build_fw_rule_states(config, marks);
        realized_rule_states = &configured_rule_states;
    }
    const auto& rule_states = *realized_rule_states;
    const auto& route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    // default_gateway rules are decided against the routes read for this test;
    // built once, every address reuses them.
    std::vector<DefaultGatewayBypassEntry> ipv4_bypass;
    std::vector<DefaultGatewayBypassEntry> ipv6_bypass;
    DefaultGatewayEval gateway_lists;
    if (network != nullptr) {
        ipv4_bypass = default_gateway_bypass_entries(
            DefaultGatewayFamily::Ipv4, network->main_routes, network->interfaces);
        ipv6_bypass = default_gateway_bypass_entries(
            DefaultGatewayFamily::Ipv6, network->main_routes, network->interfaces);
        gateway_lists.ipv4_entries = &ipv4_bypass;
        gateway_lists.ipv6_entries = &ipv6_bypass;
    }

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
        auto expected = find_expected_outbound(config, lookups, "", domain_cands, packet,
                                             &gateway_lists);
        entry.expected_outbound = expected.outbound;
        entry.list_match = expected.list_match;
        entry.matched_rule_index = expected.rule_index;
        entry.criteria_match = expected.criteria_match;
        apply_undetermined(entry, expected);
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

    std::vector<PerIpRoutingResult> per_ip_results(eval_ips.size());
    const auto check_ip = [&](size_t ip_index) {
        const auto& ip = eval_ips[ip_index];
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

        auto expected = find_expected_outbound(config, lookups, ip, domain_cands, packet,
                                               &gateway_lists);
        per_ip.entry.expected_outbound = expected.outbound;
        per_ip.entry.list_match = std::move(expected.list_match);
        per_ip.entry.matched_rule_index = expected.rule_index;
        per_ip.entry.criteria_match = expected.criteria_match;
        apply_undetermined(per_ip.entry, expected);
        if (packet.has_any() && per_ip.entry.expected_outbound == "(default)") {
            per_ip.entry.criteria_match = false;
        }
        if (!packet.has_any()) {
            per_ip.entry.criteria_match.reset();
        }

        ActualCriteriaOverride actual_criteria;
        std::map<size_t, std::optional<bool>> applied_uses_gateway;
        per_ip.rule_ip_diagnostics.reserve(result.rule_diagnostics.size());
        for (size_t idx = 0; idx < result.rule_diagnostics.size(); ++idx) {
            RuleIpDiagnostic ip_diag;
            ip_diag.ip = ip;
            ip_diag.list_match = find_rule_match(
                route_rules[idx], lookups, ip, domain_cands);
            ip_diag.in_lists = ip_diag.list_match.has_value();
            DefaultGatewayEval expected_gateway = gateway_lists;
            ip_diag.criteria_match = packet.has_any()
                ? match_rule_criteria(route_rules[idx], ip, packet, nullptr,
                                      &expected_gateway)
                : std::nullopt;
            const auto state_it = std::find_if(
                rule_states.begin(), rule_states.end(),
                [idx](const RuleState& state) { return state.rule_index == idx; });
            const bool target_only_rule =
                !packet.has_any() &&
                target_only_rule_evaluable(route_rules[idx], ip, &gateway_lists);
            if (target_only_rule && state_it != rule_states.end() &&
                state_it->action_type != RuleActionType::Skip) {
                // Judged like in packet mode, but kept out of the per-rule
                // diagnostic (criteria_match stays null for target-only).
                DefaultGatewayEval applied;
                const auto family =
                    build_firewall_rule_criteria(route_rules[idx]).default_gateway;
                const auto applied_entries = applied_bypass_entries(*state_it);
                if (family != DefaultGatewayFamily::None) {
                    (family == DefaultGatewayFamily::Ipv4 ? applied.ipv4_entries
                                                          : applied.ipv6_entries) =
                        &applied_entries;
                }
                if (family != DefaultGatewayFamily::None && !have_realized_rules) {
                    actual_criteria[idx] = std::nullopt;
                } else {
                    actual_criteria[idx] =
                        match_rule_criteria(route_rules[idx], ip, packet, nullptr, &applied);
                    if (applied.evaluated) applied_uses_gateway[idx] = applied.uses_default_gateway;
                }
            }
            if (have_realized_rules && packet.has_any() && state_it != rule_states.end() &&
                state_it->action_type != RuleActionType::Skip &&
                build_firewall_rule_criteria(route_rules[idx]).default_gateway !=
                    DefaultGatewayFamily::None) {
                // The firewall decides these against the bypass list it last
                // applied, which may differ from the routes read just now.
                const auto applied_entries = applied_bypass_entries(*state_it);
                DefaultGatewayEval applied;
                if (state_it->criteria.default_gateway == DefaultGatewayFamily::Ipv4) {
                    applied.ipv4_entries = &applied_entries;
                } else if (state_it->criteria.default_gateway == DefaultGatewayFamily::Ipv6) {
                    applied.ipv6_entries = &applied_entries;
                }
                actual_criteria[idx] =
                    match_rule_criteria(route_rules[idx], ip, packet, nullptr, &applied);
                if (applied.evaluated) applied_uses_gateway[idx] = applied.uses_default_gateway;
            }
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
            have_realized_rules, packet.has_any(), actual_criteria);
        per_ip.entry.ok = per_ip.entry.expected_outbound != "(unknown)" &&
                          per_ip.entry.expected_outbound == per_ip.entry.actual_outbound;

        if (per_ip.entry.default_gateway.has_value()) {
            const auto applied_it = applied_uses_gateway.find(
                static_cast<size_t>(per_ip.entry.default_gateway->rule_index));
            if (applied_it != applied_uses_gateway.end()) {
                per_ip.entry.default_gateway->applied_uses_default_gateway = applied_it->second;
            }
        }

        if (group_state != nullptr) {
            const auto known = [](const std::string& tag) {
                return tag != "(unknown)" && tag != "(default)";
            };
            const std::string& served = known(per_ip.entry.actual_outbound)
                ? per_ip.entry.actual_outbound : per_ip.entry.expected_outbound;
            if (known(served)) {
                const RuleState* serving = nullptr;
                for (const auto& state : rule_states) {
                    if (state.action_type == RuleActionType::Skip ||
                        state.outbound_tag != served) {
                        continue;
                    }
                    if (serving == nullptr) serving = &state;
                    if (per_ip.entry.matched_rule_index.has_value() &&
                        state.rule_index == static_cast<size_t>(*per_ip.entry.matched_rule_index)) {
                        serving = &state;
                        break;
                    }
                }
                per_ip.entry.outbound_group = build_outbound_group(
                    config, served, *group_state, serving, have_realized_rules,
                    is_ipv4_address(ip));
            }
        }
    };

    const size_t worker_count = std::min(kTestRoutingMaxConcurrentIps, eval_ips.size());
    if (worker_count > 1) {
        std::atomic_size_t next_ip{0};
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&]() {
                while (true) {
                    const size_t ip_index = next_ip.fetch_add(1);
                    if (ip_index >= eval_ips.size()) {
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

    for (auto& entry : result.entries) {
        entry.diagnosis = diagnose_routing_entry(result, entry);
    }

    result.no_matching_rule = std::none_of(
        result.entries.begin(), result.entries.end(), [](const TestRoutingEntry& entry) {
            return entry.expected_outbound != "(default)";
        });

    return result;
}

std::string describe_packet_criteria(const TestRoutingCriteria& criteria) {
    std::vector<std::string> parts;
    if (criteria.proto) parts.push_back("proto=" + *criteria.proto);
    if (criteria.src_addr) parts.push_back("src_addr=" + *criteria.src_addr);
    if (criteria.src_port) parts.push_back("src_port=" + std::to_string(*criteria.src_port));
    if (criteria.dest_port) parts.push_back("dest_port=" + std::to_string(*criteria.dest_port));
    if (criteria.dscp) parts.push_back("dscp=" + std::to_string(*criteria.dscp));
    std::string out;
    for (const auto& part : parts) {
        if (!out.empty()) out += ' ';
        out += part;
    }
    return out;
}

std::string describe_bypass(const std::string& ip, const DefaultGatewayBypassEntry& bypass) {
    switch (bypass.source) {
    case DefaultGatewayBypassSource::Route:
        return ip + " has its own route " + bypass.cidr +
               (bypass.interface ? " (" + *bypass.interface + ")" : std::string());
    case DefaultGatewayBypassSource::Address:
        return ip + " is in " + bypass.cidr + ", the network of " +
               bypass.interface.value_or("a router interface");
    case DefaultGatewayBypassSource::Reserved:
        break;
    }
    return ip + " is in a reserved address range " + bypass.cidr;
}

namespace {

// Lines that explain a row beyond the table: how a default_gateway rule
// treated the address, rules that could not be evaluated, and how a group
// outbound spreads traffic.  Empty when there is nothing to explain.
std::vector<std::string> describe_entry_details(const TestRoutingEntry& entry) {
    std::vector<std::string> lines;
    const auto rule_label = [](int index) { return "Rule #" + std::to_string(index + 1); };

    if (entry.default_gateway) {
        const auto& dg = *entry.default_gateway;
        const std::string label = rule_label(dg.rule_index) + " (default gateway)";
        if (dg.uses_default_gateway.has_value() && *dg.uses_default_gateway) {
            const bool taken = entry.matched_rule_index &&
                               *entry.matched_rule_index == dg.rule_index;
            lines.push_back(label + (taken ? " -> " + entry.expected_outbound : std::string()) +
                            ": no own route, uses the default gateway");
        } else if (dg.uses_default_gateway.has_value() && dg.bypass) {
            lines.push_back(label + " skipped: " + describe_bypass(entry.ip, *dg.bypass));
        }
        if (dg.uses_default_gateway.has_value() &&
            dg.applied_uses_default_gateway.has_value() &&
            *dg.uses_default_gateway != *dg.applied_uses_default_gateway) {
            lines.push_back(*dg.applied_uses_default_gateway
                ? "Firewall: still treats " + entry.ip +
                      " as default-gateway traffic (last applied routes)"
                : "Firewall: does not treat " + entry.ip +
                      " as default-gateway traffic yet (last applied routes)");
        }
    }

    if (entry.undetermined_rule_index) {
        const std::string label = rule_label(*entry.undetermined_rule_index);
        if (!entry.missing_criteria.empty()) {
            std::string fields;
            for (const auto& field : entry.missing_criteria) {
                if (!fields.empty()) fields += ", ";
                fields += field;
            }
            lines.push_back(label + " cannot be evaluated: missing " + fields);
        } else if (entry.default_gateway_directness) {
            lines.push_back(label + " (default gateway) cannot be evaluated: the router's "
                            "routes could not be read");
        } else {
            lines.push_back(label + " cannot be evaluated");
        }
    }

    if (!lines.empty() && entry.expected_outbound == "(default)") {
        lines.push_back("No rule matched, the system route is used");
    }

    if (entry.outbound_group) {
        const auto& group = *entry.outbound_group;
        const bool balance = group.strategy == "balance";
        size_t width = 0;
        for (const auto& member : group.members) width = std::max(width, member.tag.size());
        if (balance) {
            lines.push_back(group.tag + " (balance), each new connection uses one of:");
        } else {
            std::string selected;
            for (const auto& member : group.members) {
                if (member.in_use) selected = member.tag;
            }
            lines.push_back(group.tag + " (priority): " +
                            (selected.empty() ? std::string("no member is selected")
                                              : "uses " + selected));
        }
        bool any_in_use = false;
        for (const auto& member : group.members) {
            any_in_use = any_in_use || member.in_use;
            std::string line = "  " + member.tag +
                               std::string(width - member.tag.size() + 2, ' ');
            if (balance) {
                line += "weight " + std::to_string(member.weight.value_or(1)) + "  " +
                        (member.in_use
                             ? std::to_string(member.share_percent.value_or(0)) + "%"
                             : std::string("not in use"));
            } else if (!member.in_use) {
                line += "standby";
            } else {
                line += "selected";
            }
            lines.push_back(std::move(line));
        }
        if (balance && !any_in_use) {
            lines.push_back("  No member is in use, the group's fallback is used");
        }
    }
    return lines;
}

// The per-rule table and the per-IP breakdown, one line per fact.
std::string format_verbose_table(const TestRoutingResult& result) {
    std::ostringstream out;
    constexpr int ip_w        = 25;
    constexpr int list_w      = 35;
    constexpr int outbound_w  = 18;

    out << keen_pbr3::format("{:<{}} | {:<{}} | {:<{}} | {:<{}} | {}\n",
                             "IP", ip_w,
                             "List Match", list_w,
                             "Expected Outbound", outbound_w,
                             "Actual Outbound", outbound_w,
                             "Status");
    out << std::string(ip_w + 3 + list_w + 3 + outbound_w + 3 + outbound_w + 3 + 6, '-')
        << "\n";

    for (const auto& entry : result.entries) {
        std::string list_str = "-";
        if (entry.list_match) {
            list_str = entry.list_match->list_name;
            if (!entry.list_match->via.empty() && entry.list_match->via != entry.ip) {
                list_str += " (via " + entry.list_match->via + ")";
            }
        }

        const std::string status = entry.ok ? "OK" : "NOK";

        out << keen_pbr3::format("{:<{}} | {:<{}} | {:<{}} | {:<{}} | {}\n",
                                 entry.ip, ip_w,
                                 list_str, list_w,
                                 entry.expected_outbound, outbound_w,
                                 entry.actual_outbound, outbound_w,
                                 status);
    }

    for (const auto& entry : result.entries) {
        const auto details = describe_entry_details(entry);
        if (details.empty()) continue;
        out << "\n" << entry.ip << "\n";
        for (const auto& line : details) {
            out << "  " << line << "\n";
        }
    }
    return out.str();
}

// 0 when the lookup succeeded and every entry routes as expected.
int test_routing_exit_code(const TestRoutingResult& result) {
    if (result.dns_error.has_value()) return 1;
    const bool all_ok = std::all_of(result.entries.begin(), result.entries.end(),
                                    [](const TestRoutingEntry& entry) { return entry.ok; });
    return all_ok ? 0 : 1;
}

int render_test_routing_result(const TestRoutingResult& result,
                               const TestRoutingCriteria& criteria = {},
                               const TestRoutingOutputOptions& output = {}) {
    if (output.json) {
        std::cout << test_routing_result_to_json(result).dump(2) << '\n';
        return test_routing_exit_code(result);
    }
    for (const auto& w : result.warnings) {
        std::cerr << "Warning: " << w << "\n";
    }
    if (result.dns_error.has_value()) {
        std::cerr << "DNS error: " << *result.dns_error << "\n";
    }

    std::cout << format_test_routing_report(result, criteria, term::stdout_style());
    if (output.verbose) {
        std::cout << "\n" << format_verbose_table(result);
    }
    return test_routing_exit_code(result);
}
} // namespace

int run_test_routing_command(const Config& config,
                              const CacheManager& cache,
                              const std::string& target,
                              const TestRoutingOutputOptions& output) {
    return render_test_routing_result(compute_test_routing(config, cache, target), {}, output);
}

int run_test_routing_command(const nlohmann::json& response,
                             const TestRoutingCriteria& criteria,
                             const TestRoutingOutputOptions& output) {
    if (output.json) {
        // The daemon's body is already the RoutingTestResponse; pass it through
        // untouched so rule_diagnostics survive.
        const auto& body = response.at("result");
        std::cout << body.dump(2) << '\n';
        return test_routing_exit_code(test_routing_result_from_json(body));
    }
    return render_test_routing_result(
        test_routing_result_from_json(response.at("result")), criteria, output);
}

} // namespace keen_pbr3
