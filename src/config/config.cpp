#include "config.hpp"
#include "addr_spec.hpp"
#include "routing_state.hpp"

#include <cctype>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "../dns/dns_probe_server.hpp"
#include "../util/cron.hpp"

namespace keen_pbr3 {

using json = nlohmann::json;

namespace {

void add_issue(std::vector<ConfigValidationIssue>& issues,
               std::string path,
               std::string message) {
    issues.push_back({std::move(path), std::move(message)});
}

void validate_optional_integer_field(const json& root,
                                     const char* parent_key,
                                     const char* child_key,
                                     const std::string& path,
                                     std::vector<ConfigValidationIssue>& issues) {
    const auto parent_it = root.find(parent_key);
    if (parent_it == root.end() || !parent_it->is_object()) {
        return;
    }

    const auto child_it = parent_it->find(child_key);
    if (child_it == parent_it->end() || child_it->is_null()) {
        return;
    }

    if (!child_it->is_number_integer()) {
        add_issue(issues, path, path + " must be an integer");
    }
}

void validate_optional_string_field(const json& root,
                                    const char* parent_key,
                                    const char* child_key,
                                    const std::string& path,
                                    std::vector<ConfigValidationIssue>& issues) {
    const auto parent_it = root.find(parent_key);
    if (parent_it == root.end() || !parent_it->is_object()) {
        return;
    }

    const auto child_it = parent_it->find(child_key);
    if (child_it == parent_it->end() || child_it->is_null()) {
        return;
    }

    if (!child_it->is_string()) {
        add_issue(issues, path, path + " must be a string");
    }
}

void validate_optional_hex_string_field(const json& root,
                                        const char* parent_key,
                                        const char* child_key,
                                        const std::string& path,
                                        std::vector<ConfigValidationIssue>& issues) {
    const auto parent_it = root.find(parent_key);
    if (parent_it == root.end() || !parent_it->is_object()) {
        return;
    }

    const auto child_it = parent_it->find(child_key);
    if (child_it == parent_it->end() || child_it->is_null()) {
        return;
    }

    if (!child_it->is_string()) {
        add_issue(issues, path, path + " must be a string in hex format (e.g. 0x00010000)");
    }
}

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\n\r\f\v");
    if (begin == std::string::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\n\r\f\v");
    return value.substr(begin, end - begin + 1);
}

bool parse_uint_in_range(const std::string& raw, int min_value, int max_value, int& out) {
    if (raw.empty()) {
        return false;
    }

    long long value = 0;
    for (char c : raw) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }

        value = value * 10 + (c - '0');
        if (value > std::numeric_limits<int>::max()) {
            return false;
        }
    }

    if (value < min_value || value > max_value) {
        return false;
    }

    out = static_cast<int>(value);
    return true;
}

constexpr size_t IPSET_MAX_NAME = 31;
constexpr size_t IPSET_PREFIX_LEN = 7; // len("kpbr4d_")
constexpr size_t MAX_TAG_LEN = IPSET_MAX_NAME - IPSET_PREFIX_LEN; // 24

bool is_valid_tag(const std::string& value) {
    if (value.empty() || value.size() > MAX_TAG_LEN) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(value[0]);
    if (first < 'a' || first > 'z') {
        return false;
    }

    for (size_t i = 1; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        const bool valid = (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') ||
                           c == '_';
        if (!valid) {
            return false;
        }
    }

    return true;
}

void validate_tag(std::vector<ConfigValidationIssue>& issues,
                  const std::string& path,
                  const std::string& kind,
                  const std::string& value) {
    if (value.empty()) {
        add_issue(issues, path, kind + " must not be empty");
        return;
    }

    if (value.size() > MAX_TAG_LEN) {
        add_issue(issues, path,
                  kind + " '" + value + "' is too long: " +
                      std::to_string(value.size()) + " chars, maximum is " +
                      std::to_string(MAX_TAG_LEN));
    }

    if (!is_valid_tag(value)) {
        add_issue(issues, path,
                  kind + " '" + value +
                      "' must match naming convention [a-z][a-z0-9_]*");
    }
}

std::optional<std::string> validate_port_spec(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }

    const std::string normalized = trim_copy(*value);
    if (normalized.empty()) {
        return std::nullopt;
    }

    const std::string content = normalized[0] == '!' ? normalized.substr(1) : normalized;
    if (content.empty() || content.front() == ',' || content.back() == ',') {
        return std::string("Use comma-separated ports or ranges.");
    }

    std::stringstream ss(content);
    std::string token;
    while (std::getline(ss, token, ',')) {
        const std::string part = trim_copy(token);
        if (part.empty()) {
            return std::string("Use comma-separated ports or ranges.");
        }

        const auto dash = part.find('-');
        if (dash != std::string::npos) {
            if (part.find('-', dash + 1) != std::string::npos) {
                return std::string("Port ranges must use valid ports such as 8000-9000.");
            }

            const std::string start_part = trim_copy(part.substr(0, dash));
            const std::string end_part = trim_copy(part.substr(dash + 1));
            int start = 0;
            int end = 0;
            if (!parse_uint_in_range(start_part, 1, 65535, start) ||
                !parse_uint_in_range(end_part, 1, 65535, end)) {
                return std::string("Port ranges must use valid ports such as 8000-9000.");
            }

            if (start > end) {
                return std::string("Port range start must be less than or equal to end.");
            }

            continue;
        }

        int port = 0;
        if (!parse_uint_in_range(part, 1, 65535, port)) {
            return std::string("Ports must be integers between 1 and 65535.");
        }
    }

    return std::nullopt;
}

std::optional<std::string> validate_address_spec(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }

    const std::string normalized = trim_copy(*value);
    if (normalized.empty()) {
        return std::nullopt;
    }

    const std::string content = normalized[0] == '!' ? normalized.substr(1) : normalized;
    if (content.empty() || content.front() == ',' || content.back() == ',') {
        return std::string("Use comma-separated IP addresses or CIDRs.");
    }

    try {
        (void)parse_addr_spec(normalized);
    } catch (const std::invalid_argument&) {
        return std::string("Addresses must be valid IPv4 or IPv6 hosts or CIDR ranges, for example 10.0.0.1, 10.0.0.0/8, or 2001:db8::/32.");
    }

    return std::nullopt;
}

std::optional<std::string> get_optional_string_field(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null() || !it->is_string()) {
        return std::nullopt;
    }

    return it->get<std::string>();
}

void validate_route_rule_specs(const json& root, std::vector<ConfigValidationIssue>& issues) {
    const auto route_it = root.find("route");
    if (route_it == root.end() || !route_it->is_object()) {
        return;
    }

    const auto rules_it = route_it->find("rules");
    if (rules_it == route_it->end() || !rules_it->is_array()) {
        return;
    }

    for (size_t index = 0; index < rules_it->size(); ++index) {
        const auto& rule = rules_it->at(index);
        if (!rule.is_object()) {
            continue;
        }

        const std::string rule_path = "route.rules[" + std::to_string(index) + "]";

        if (auto error = validate_port_spec(get_optional_string_field(rule, "src_port"))) {
            add_issue(issues, rule_path + ".src_port", *error);
        }

        if (auto error = validate_port_spec(get_optional_string_field(rule, "dest_port"))) {
            add_issue(issues, rule_path + ".dest_port", *error);
        }

        if (auto error = validate_address_spec(get_optional_string_field(rule, "src_addr"))) {
            add_issue(issues, rule_path + ".src_addr", *error);
        }

        if (auto error = validate_address_spec(get_optional_string_field(rule, "dest_addr"))) {
            add_issue(issues, rule_path + ".dest_addr", *error);
        }
    }
}

} // namespace

ConfigValidationError::ConfigValidationError(std::vector<ConfigValidationIssue> issues)
    : ConfigError(build_message(issues))
    , issues_(std::move(issues)) {}

std::string ConfigValidationError::build_message(
    const std::vector<ConfigValidationIssue>& issues) {
    if (issues.empty()) {
        return "Config validation failed";
    }

    if (issues.size() == 1) {
        return issues.front().message;
    }

    return "Config validation failed with " + std::to_string(issues.size()) + " errors";
}

// Validate that the fwmark mask has exactly two adjacent hex nibbles set to F.
static void validate_fwmark_mask(uint32_t mask) {
    if (mask == 0) {
        throw ConfigError("fwmark.mask must not be zero");
    }

    uint32_t lowest = mask & (~mask + 1);
    uint32_t shifted = mask / lowest;

    if (shifted != 0xFF) {
        std::ostringstream oss;
        oss << "fwmark.mask must have exactly two adjacent hex nibbles set to F "
            << "(e.g. 0x00FF0000, 0x0000FF00), got 0x"
            << std::hex << std::setfill('0') << std::setw(8) << mask;
        throw ConfigError(oss.str());
    }

    int bit_pos = 0;
    uint32_t tmp = lowest;
    while (tmp > 1) {
        tmp >>= 1;
        ++bit_pos;
    }
    if (bit_pos % 4 != 0) {
        std::ostringstream oss;
        oss << "fwmark.mask must be aligned to nibble boundaries "
            << "(e.g. 0x00FF0000, 0x0000FF00), got 0x"
            << std::hex << std::setfill('0') << std::setw(8) << mask;
        throw ConfigError(oss.str());
    }
}

uint32_t parse_fwmark_hex_or_throw(const std::optional<std::string>& raw,
                                   uint32_t default_value,
                                   const std::string& path) {
    if (!raw.has_value()) {
        return default_value;
    }

    const std::string value = trim_copy(*raw);
    if (value.empty()) {
        throw ConfigError(path + " must not be empty");
    }

    if (value.size() < 3 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) {
        throw ConfigError(path + " must be a hexadecimal string with 0x prefix");
    }

    for (size_t i = 2; i < value.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            throw ConfigError(path + " must contain only hexadecimal digits");
        }
    }

    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, nullptr, 16);
    } catch (const std::exception&) {
        throw ConfigError(path + " must be a valid 32-bit hexadecimal value");
    }

    if (parsed > std::numeric_limits<uint32_t>::max()) {
        throw ConfigError(path + " must fit into 32 bits (max 0xFFFFFFFF)");
    }

    return static_cast<uint32_t>(parsed);
}

uint32_t parse_fwmark_start_or_throw(const FwmarkConfig& fwmark_cfg) {
    return parse_fwmark_hex_or_throw(fwmark_cfg.start, 0x00010000, "fwmark.start");
}

uint32_t parse_fwmark_mask_or_throw(const FwmarkConfig& fwmark_cfg) {
    return parse_fwmark_hex_or_throw(fwmark_cfg.mask, 0x00FF0000, "fwmark.mask");
}

Config parse_config(const std::string& json_str) {
    Config cfg;
    json parsed_json;
    std::vector<ConfigValidationIssue> issues;

    try {
        parsed_json = json::parse(json_str, nullptr, true, true);
    } catch (const json::parse_error& e) {
        throw ConfigValidationError(std::vector<ConfigValidationIssue>{
            {"$", std::string("Invalid JSON: ") + e.what()}
        });
    }

    validate_optional_hex_string_field(
        parsed_json, "fwmark", "start", "fwmark.start", issues);
    validate_optional_hex_string_field(
        parsed_json, "fwmark", "mask", "fwmark.mask", issues);
    validate_optional_integer_field(
        parsed_json, "iproute", "table_start", "iproute.table_start", issues);
    validate_optional_integer_field(
        parsed_json, "daemon", "firewall_verify_max_bytes",
        "daemon.firewall_verify_max_bytes", issues);
    validate_optional_integer_field(
        parsed_json, "daemon", "max_file_size_bytes", "daemon.max_file_size_bytes", issues);
    validate_optional_string_field(
        parsed_json, "daemon", "firewall_backend", "daemon.firewall_backend", issues);
    validate_route_rule_specs(parsed_json, issues);

    if (!issues.empty()) {
        throw ConfigValidationError(std::move(issues));
    }

    try {
        cfg = parsed_json.get<Config>();
    } catch (const json::exception& e) {
        throw ConfigValidationError(std::vector<ConfigValidationIssue>{
            {"$", e.what()}
        });
    }

    return cfg;
}

void validate_config(const Config& cfg) {
    std::vector<ConfigValidationIssue> issues;

    if (cfg.daemon && cfg.daemon->firewall_verify_max_bytes.has_value() &&
        *cfg.daemon->firewall_verify_max_bytes < 0) {
        add_issue(issues, "daemon.firewall_verify_max_bytes",
                  "daemon.firewall_verify_max_bytes must be >= 0");
    }

    if (cfg.daemon && cfg.daemon->max_file_size_bytes.has_value() &&
        *cfg.daemon->max_file_size_bytes <= 0) {
        add_issue(issues, "daemon.max_file_size_bytes",
                  "daemon.max_file_size_bytes must be greater than 0");
    }

    if (cfg.daemon && cfg.daemon->firewall_backend.has_value()) {
        const std::string backend = trim_copy(*cfg.daemon->firewall_backend);
        if (backend != "auto" && backend != "iptables" && backend != "nftables") {
            add_issue(issues, "daemon.firewall_backend",
                      "daemon.firewall_backend must be one of: auto, iptables, nftables");
        }
    }

    if (cfg.lists_autoupdate) {
        const bool enabled = cfg.lists_autoupdate->enabled.value_or(false);
        const std::string cron = cfg.lists_autoupdate->cron.value_or("");
        if (enabled && cron.empty()) {
            add_issue(issues, "lists_autoupdate.cron",
                      "lists_autoupdate.cron is required when enabled");
        }
        if (!cron.empty()) {
            try {
                cron_validate(cron);
            } catch (const std::invalid_argument& e) {
                add_issue(issues, "lists_autoupdate.cron",
                          std::string("lists_autoupdate.cron: ") + e.what());
            }
        }
    }

    for (const auto& [name, list_cfg] : cfg.lists.value_or(std::map<std::string, ListConfig>{})) {
        const std::string list_path = name.empty() ? "lists" : "lists." + name;
        validate_tag(issues, list_path, "List name", name);

        const bool has_url = list_cfg.url.has_value();
        const bool has_file = list_cfg.file.has_value();
        const bool has_cidrs =
            list_cfg.ip_cidrs.has_value() && !list_cfg.ip_cidrs->empty();
        const bool has_domains =
            list_cfg.domains.has_value() && !list_cfg.domains->empty();
        if (!has_url && !has_file && !has_cidrs && !has_domains) {
            add_issue(issues, list_path,
                      "List '" + name +
                          "' must have at least one of: url, domains, ip_cidrs, file");
        }
    }

    const auto& outbounds = cfg.outbounds.value_or(std::vector<Outbound>{});
    for (const auto& ob : outbounds) {
        validate_tag(issues, "outbounds." + ob.tag + ".tag", "Outbound tag", ob.tag);

        if (ob.type != OutboundType::URLTEST) continue;

        if (!ob.outbound_groups.has_value() || ob.outbound_groups->empty()) {
            add_issue(issues, "outbounds." + ob.tag + ".outbound_groups",
                      "Urltest outbound '" + ob.tag +
                          "' 'outbound_groups' array must not be empty");
            continue;
        }

        for (size_t group_index = 0; group_index < ob.outbound_groups->size(); ++group_index) {
            const auto& group = ob.outbound_groups->at(group_index);
            const std::string group_path =
                "outbounds." + ob.tag + ".outbound_groups[" + std::to_string(group_index) + "]";

            if (group.outbounds.empty()) {
                add_issue(issues, group_path + ".outbounds",
                          "Urltest outbound '" + ob.tag +
                              "' outbound_group has empty 'outbounds' array");
            }

            for (const auto& ref_tag : group.outbounds) {
                bool found = false;
                for (const auto& target : outbounds) {
                    if (target.tag != ref_tag) {
                        continue;
                    }

                    found = true;
                    if (target.type != OutboundType::INTERFACE &&
                        target.type != OutboundType::TABLE &&
                        target.type != OutboundType::BLACKHOLE) {
                        add_issue(
                            issues,
                            group_path + ".outbounds",
                            "Urltest outbound '" + ob.tag +
                                "' references outbound '" + ref_tag +
                                "' which is not an interface, table, or blackhole outbound");
                    }
                    break;
                }

                if (!found) {
                    add_issue(
                        issues,
                        group_path + ".outbounds",
                        "Urltest outbound '" + ob.tag +
                            "' references unknown outbound tag '" + ref_tag + "'");
                }
            }
        }
    }

    const FwmarkConfig fwmark_cfg = cfg.fwmark.value_or(FwmarkConfig{});
    bool fwmark_start_valid = true;
    try {
        (void)parse_fwmark_start_or_throw(fwmark_cfg);
    } catch (const ConfigError& e) {
        fwmark_start_valid = false;
        add_issue(issues, "fwmark.start", e.what());
    }

    uint32_t fwmark_mask = 0;
    bool fwmark_mask_valid = true;
    try {
        fwmark_mask = parse_fwmark_mask_or_throw(fwmark_cfg);
        validate_fwmark_mask(fwmark_mask);
    } catch (const ConfigError& e) {
        fwmark_mask_valid = false;
        add_issue(issues, "fwmark.mask", e.what());
    }

    if (fwmark_start_valid && fwmark_mask_valid) {
        try {
            (void)allocate_outbound_marks(fwmark_cfg, outbounds);
        } catch (const ConfigError& e) {
            add_issue(issues, "outbounds", e.what());
        }
    }

    {
        const uint32_t table_start = static_cast<uint32_t>(
            cfg.iproute.value_or(IprouteConfig{}).table_start.value_or(100));
        if (is_reserved_table(table_start)) {
            add_issue(issues, "iproute.table_start",
                      "iproute.table_start " + std::to_string(table_start) +
                          " is reserved. Use a different value (e.g. 100).");
        }
    }

    if (cfg.dns.has_value()) {
        const auto& dns_servers = cfg.dns->servers.value_or(std::vector<DnsServer>{});
        std::set<std::string> dns_server_tags;
        std::set<std::string> dns_server_identities;
        for (const auto& srv : dns_servers) {
            validate_tag(issues, "dns.servers." + srv.tag + ".tag", "DNS server tag", srv.tag);
            if (!dns_server_tags.insert(srv.tag).second) {
                add_issue(issues, "dns.servers." + srv.tag + ".tag",
                          "Duplicate DNS server tag \"" + srv.tag + "\"");
            }

            const auto srv_type = srv.type.value_or(api::DnsServerType::STATIC);
            const std::string srv_addr = srv.address.value_or("");
            const std::string srv_detour = srv.detour.value_or("");
            const std::string srv_identity =
                std::to_string(static_cast<int>(srv_type)) + "|" + srv_addr + "|" + srv_detour;
            if (!dns_server_identities.insert(srv_identity).second) {
                add_issue(issues, "dns.servers." + srv.tag,
                          "DNS server \"" + srv.tag +
                              "\" duplicates an existing DNS server definition (same type/address/detour)");
            }

            if (srv_type == api::DnsServerType::KEENETIC) {
#ifndef USE_KEENETIC_API
                add_issue(issues, "dns.servers." + srv.tag + ".type",
                          "dns.servers[\"" + srv.tag +
                              "\"].type='keenetic' requires build with USE_KEENETIC_API=ON");
#endif
                if (srv.address.has_value() && !srv.address->empty()) {
                    add_issue(issues, "dns.servers." + srv.tag + ".address",
                              "dns.servers[\"" + srv.tag +
                                  "\"].address must not be set for type='keenetic' (resolved via RCI)");
                }
            } else if (srv_type == api::DnsServerType::STATIC) {
                if (!srv.address.has_value() || srv.address->empty()) {
                    add_issue(issues, "dns.servers." + srv.tag + ".address",
                              "dns.servers[\"" + srv.tag +
                                  "\"].address is required for type='static'");
                }
            } else {
                add_issue(issues, "dns.servers." + srv.tag + ".type",
                          "dns.servers[\"" + srv.tag +
                              "\"].type must be one of: static, keenetic");
            }

            if (!srv.detour.has_value()) continue;

            const std::string& dtag = srv.detour.value();
            bool found = false;
            for (const auto& ob : outbounds) {
                if (ob.tag != dtag) {
                    continue;
                }

                found = true;
                if (ob.type == OutboundType::BLACKHOLE ||
                    ob.type == OutboundType::IGNORE) {
                    add_issue(
                        issues,
                        "dns.servers." + srv.tag + ".detour",
                        "dns.servers[\"" + srv.tag + "\"].detour: outbound \""
                            + dtag + "\" has no routing table");
                }
                break;
            }

            if (!found) {
                add_issue(
                    issues,
                    "dns.servers." + srv.tag + ".detour",
                    "dns.servers[\"" + srv.tag + "\"].detour: unknown outbound tag \""
                        + dtag + "\"");
            }
        }

        if (cfg.dns->fallback.has_value()) {
            std::set<std::string> seen_fallback_tags;
            for (size_t i = 0; i < cfg.dns->fallback->size(); ++i) {
                const std::string& fallback_tag = (*cfg.dns->fallback)[i];
                const std::string path = "dns.fallback." + std::to_string(i);

                if (fallback_tag.empty()) {
                    add_issue(issues, path,
                              "dns.fallback[" + std::to_string(i) + "] must not be empty");
                    continue;
                }

                if (!seen_fallback_tags.insert(fallback_tag).second) {
                    add_issue(issues, path,
                              "dns.fallback[" + std::to_string(i) +
                                  "] duplicates DNS server tag \"" + fallback_tag + "\"");
                }

                if (dns_server_tags.find(fallback_tag) == dns_server_tags.end()) {
                    add_issue(issues, path,
                              "dns.fallback[" + std::to_string(i) +
                                  "] references unknown DNS server tag \"" + fallback_tag + "\"");
                }
            }
        }

        if (cfg.dns->system_resolver.has_value()) {
            const auto& resolver = *cfg.dns->system_resolver;

            if (resolver.address.empty()) {
                add_issue(issues, "dns.system_resolver.address",
                          "dns.system_resolver.address must not be empty");
            }

            switch (resolver.type) {
                case DnsSystemResolverType::DNSMASQ_IPSET:
                case DnsSystemResolverType::DNSMASQ_NFTSET:
                    break;
                default:
                    add_issue(issues, "dns.system_resolver.type",
                              "dns.system_resolver.type must be one of: dnsmasq-ipset, dnsmasq-nftset");
                    break;
            }
        } else {
            add_issue(issues, "dns.system_resolver",
                      "dns.system_resolver must be present");
        }

        if (cfg.dns->dns_test_server.has_value()) {
            try {
                const auto& test_cfg = *cfg.dns->dns_test_server;
                const std::string* answer_ip =
                    test_cfg.answer_ipv4 ? &*test_cfg.answer_ipv4 : nullptr;
                (void)parse_dns_probe_server_settings(test_cfg.listen, answer_ip);
            } catch (const std::exception& e) {
                add_issue(issues, "dns.dns_test_server",
                          std::string("dns.dns_test_server: ") + e.what());
            }
        }
    } else {
        add_issue(issues, "dns.system_resolver",
                  "dns.system_resolver must be present");
    }

    if (!issues.empty()) {
        throw ConfigValidationError(std::move(issues));
    }
}

size_t max_file_size_bytes(const Config& config) {
    const auto bytes = config.daemon.value_or(DaemonConfig{})
                           .max_file_size_bytes.value_or(
                               static_cast<int64_t>(kDefaultMaxFileSizeBytes));
    return static_cast<size_t>(bytes);
}

std::string firewall_backend_preference(const Config& config) {
    const std::string backend = trim_copy(
        config.daemon.value_or(DaemonConfig{}).firewall_backend.value_or("auto"));
    return backend.empty() ? "auto" : backend;
}

Config parse_and_validate_config(const std::string& json_str) {
    Config config = parse_config(json_str);
    validate_config(config);
    return config;
}

OutboundMarkMap allocate_outbound_marks(const FwmarkConfig& fwmark_cfg,
                                         const std::vector<Outbound>& outbounds) {
    uint32_t mask  = parse_fwmark_mask_or_throw(fwmark_cfg);
    uint32_t start = parse_fwmark_start_or_throw(fwmark_cfg);

    validate_fwmark_mask(mask);

    uint32_t lowest_bit = mask & (~mask + 1);
    uint32_t step = lowest_bit;

    constexpr uint32_t max_marks = 256;

    OutboundMarkMap mark_map;
    uint32_t current_mark = start;
    uint32_t count = 0;

    for (const auto& ob : outbounds) {
        if (ob.type != OutboundType::INTERFACE &&
            ob.type != OutboundType::TABLE &&
            ob.type != OutboundType::URLTEST) continue;

        if (count >= max_marks) {
            throw ConfigError(
                "Too many routable outbounds: maximum " + std::to_string(max_marks) +
                " supported with current fwmark.mask");
        }

        mark_map[ob.tag] = current_mark;
        current_mark += step;
        ++count;
    }

    return mark_map;
}

uint32_t fwmark_start_value(const FwmarkConfig& fwmark_cfg) {
    return parse_fwmark_start_or_throw(fwmark_cfg);
}

uint32_t fwmark_mask_value(const FwmarkConfig& fwmark_cfg) {
    const uint32_t mask = parse_fwmark_mask_or_throw(fwmark_cfg);
    validate_fwmark_mask(mask);
    return mask;
}

} // namespace keen_pbr3
