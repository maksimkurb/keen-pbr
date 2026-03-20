#include "config.hpp"
#include "routing_state.hpp"

#include <cctype>
#include <iomanip>
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

Config parse_config(const std::string& json_str) {
    Config cfg;
    json parsed_json;

    try {
        parsed_json = json::parse(json_str);
    } catch (const json::parse_error& e) {
        throw ConfigValidationError(std::vector<ConfigValidationIssue>{
            {"$", std::string("Invalid JSON: ") + e.what()}
        });
    }

    try {
        cfg = parsed_json.get<Config>();
    } catch (const json::exception& e) {
        throw ConfigValidationError(std::vector<ConfigValidationIssue>{
            {"$", e.what()}
        });
    }

    std::vector<ConfigValidationIssue> issues;

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

    static constexpr size_t IPSET_MAX_NAME    = 31;
    static constexpr size_t IPSET_PREFIX_LEN  = 7; // len("kpbr4d_")
    static constexpr size_t LIST_NAME_MAX_LEN = IPSET_MAX_NAME - IPSET_PREFIX_LEN; // 24

    for (const auto& [name, list_cfg] : cfg.lists.value_or(std::map<std::string, ListConfig>{})) {
        const std::string list_path = name.empty() ? "lists" : "lists." + name;

        if (name.empty()) {
            add_issue(issues, "lists", "List name must not be empty");
            continue;
        }

        if (name.size() > LIST_NAME_MAX_LEN) {
            add_issue(issues, list_path,
                      "List name '" + name + "' is too long: " +
                          std::to_string(name.size()) + " chars, maximum is " +
                          std::to_string(LIST_NAME_MAX_LEN));
        }

        if (!std::isalpha(static_cast<unsigned char>(name[0]))) {
            add_issue(issues, list_path,
                      "List name '" + name +
                          "': first character must be a letter [a-zA-Z]");
        }

        for (size_t i = 1; i < name.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            if (!std::isalpha(c) && !std::isdigit(c) && c != '_') {
                add_issue(issues, list_path,
                          "List name '" + name +
                              "': invalid character '" + name[i] +
                              "' at position " + std::to_string(i) +
                              " (allowed: a-zA-Z, 0-9, _)");
            }
        }

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

    for (const auto& rule : cfg.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{})) {
        (void)rule;
    }

    const auto& outbounds = cfg.outbounds.value_or(std::vector<Outbound>{});
    for (const auto& ob : outbounds) {
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

    const uint32_t fwmark_mask = static_cast<uint32_t>(
        cfg.fwmark.value_or(FwmarkConfig{}).mask.value_or(0x00FF0000));
    bool fwmark_mask_valid = true;
    try {
        validate_fwmark_mask(fwmark_mask);
    } catch (const ConfigError& e) {
        fwmark_mask_valid = false;
        add_issue(issues, "fwmark.mask", e.what());
    }

    if (fwmark_mask_valid) {
        try {
            (void)allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}), outbounds);
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
        for (const auto& srv : dns_servers) {
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
    }

    if (!issues.empty()) {
        throw ConfigValidationError(std::move(issues));
    }

    return cfg;
}

OutboundMarkMap allocate_outbound_marks(const FwmarkConfig& fwmark_cfg,
                                         const std::vector<Outbound>& outbounds) {
    uint32_t mask  = static_cast<uint32_t>(fwmark_cfg.mask.value_or(0x00FF0000));
    uint32_t start = static_cast<uint32_t>(fwmark_cfg.start.value_or(0x00010000));

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

} // namespace keen_pbr3
