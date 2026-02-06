#include "dns_router.hpp"

#include <algorithm>

namespace keen_pbr3 {

DnsRouter::DnsRouter(const DnsConfig& dns_config, const ListManager& list_manager)
    : rules_(dns_config.rules),
      fallback_tag_(dns_config.fallback),
      list_manager_(list_manager) {
    // Parse all DNS server definitions into DnsServerConfig
    for (const auto& server : dns_config.servers) {
        servers_.emplace(
            server.tag,
            parse_dns_server(server.tag, server.address, server.detour));
    }

    // Validate that fallback server tag exists
    if (servers_.find(fallback_tag_) == servers_.end()) {
        throw DnsError("DNS fallback server tag not found: '" + fallback_tag_ + "'");
    }

    // Validate that all rule server tags exist
    for (const auto& rule : rules_) {
        if (servers_.find(rule.server) == servers_.end()) {
            throw DnsError("DNS rule references unknown server tag: '" + rule.server + "'");
        }
    }
}

const DnsServerConfig& DnsRouter::resolve(const std::string& domain) const {
    // Process rules in config order; first match wins
    for (const auto& rule : rules_) {
        for (const auto& list_name : rule.lists) {
            if (domain_matches_list(domain, list_name)) {
                return servers_.at(rule.server);
            }
        }
    }
    // No rule matched; return fallback
    return servers_.at(fallback_tag_);
}

const DnsServerConfig* DnsRouter::get_server(const std::string& tag) const {
    auto it = servers_.find(tag);
    if (it == servers_.end()) {
        return nullptr;
    }
    return &it->second;
}

const DnsServerConfig& DnsRouter::fallback() const {
    return servers_.at(fallback_tag_);
}

bool DnsRouter::domain_matches_list(const std::string& domain,
                                     const std::string& list_name) const {
    const ParsedList* list = list_manager_.get(list_name);
    if (!list) {
        return false;
    }

    for (const auto& pattern : list->domains) {
        if (domain_matches_pattern(domain, pattern)) {
            return true;
        }
    }
    return false;
}

bool DnsRouter::domain_matches_pattern(const std::string& domain,
                                        const std::string& pattern) {
    if (pattern.empty() || domain.empty()) {
        return false;
    }

    // Wildcard pattern: *.example.com matches any subdomain of example.com
    // Also matches example.com itself (the base domain)
    if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
        const std::string suffix = pattern.substr(1); // ".example.com"

        // Exact match on the base domain (e.g., "example.com" matches "*.example.com")
        if (domain == pattern.substr(2)) {
            return true;
        }

        // Suffix match (e.g., "sub.example.com" ends with ".example.com")
        if (domain.size() > suffix.size() &&
            domain.compare(domain.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }

        return false;
    }

    // Exact match
    return domain == pattern;
}

} // namespace keen_pbr3
