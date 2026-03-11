#include "config.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "../util/cron.hpp"

namespace keen_pbr3 {

using json = nlohmann::json;

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
    try {
        cfg = json::parse(json_str).get<Config>();
    } catch (const json::parse_error& e) {
        throw ConfigError(std::string("Invalid JSON: ") + e.what());
    } catch (const json::exception& e) {
        throw ConfigError(e.what());
    }

    // Validate: lists_autoupdate.cron
    if (cfg.lists_autoupdate) {
        bool enabled = cfg.lists_autoupdate->enabled.value_or(false);
        const std::string cron = cfg.lists_autoupdate->cron.value_or("");
        if (enabled && cron.empty()) {
            throw ConfigError("lists_autoupdate.cron is required when enabled");
        }
        if (!cron.empty()) {
            try {
                cron_validate(cron);
            } catch (const std::invalid_argument& e) {
                throw ConfigError(std::string("lists_autoupdate.cron: ") + e.what());
            }
        }
    }

    // Validate: each list name and each list's sources
    //
    // Set name budget: ipset limits names to 31 characters.
    // The longest prefix we generate is "kpbr4d_" / "kpbr6d_" (7 chars), so
    // list names must be at most 31 - 7 = 24 characters.
    // Character set: first char must be [a-z], remaining chars [a-z0-9_].
    static constexpr size_t IPSET_MAX_NAME     = 31;
    static constexpr size_t IPSET_PREFIX_LEN   = 7; // len("kpbr4d_")
    static constexpr size_t LIST_NAME_MAX_LEN  = IPSET_MAX_NAME - IPSET_PREFIX_LEN; // 24

    for (const auto& [name, list_cfg] : cfg.lists.value_or(std::map<std::string, ListConfig>{})) {
        // Name length check
        if (name.empty()) {
            throw ConfigError("List name must not be empty");
        }
        if (name.size() > LIST_NAME_MAX_LEN) {
            throw ConfigError("List name '" + name + "' is too long: " +
                              std::to_string(name.size()) + " chars, maximum is " +
                              std::to_string(LIST_NAME_MAX_LEN));
        }
        // Character set check: first char [a-zA-Z], rest [a-zA-Z0-9_]
        if (!std::isalpha(static_cast<unsigned char>(name[0]))) {
            throw ConfigError("List name '" + name +
                              "': first character must be a letter [a-zA-Z]");
        }
        for (size_t i = 1; i < name.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(name[i]);
            if (!std::isalpha(c) && !std::isdigit(c) && c != '_') {
                throw ConfigError("List name '" + name +
                                  "': invalid character '" + name[i] +
                                  "' at position " + std::to_string(i) +
                                  " (allowed: a-zA-Z, 0-9, _)");
            }
        }

        // Source check
        bool has_url    = list_cfg.url.has_value();
        bool has_file   = list_cfg.file.has_value();
        bool has_cidrs  = list_cfg.ip_cidrs.has_value() && !list_cfg.ip_cidrs->empty();
        bool has_domains = list_cfg.domains.has_value() && !list_cfg.domains->empty();
        if (!has_url && !has_file && !has_cidrs && !has_domains) {
            throw ConfigError("List '" + name +
                              "' must have at least one of: url, domains, ip_cidrs, file");
        }
    }

    // Validate: route rules reject legacy fields
    for (const auto& rule : cfg.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{})) {
        (void)rule; // field checks are now at JSON parse time via required fields
    }

    // Validate: urltest outbound_groups must reference interface/table/blackhole outbounds
    const auto& outbounds = cfg.outbounds.value_or(std::vector<Outbound>{});
    for (const auto& ob : outbounds) {
        if (ob.type != OutboundType::URLTEST) continue;
        if (!ob.outbound_groups.has_value() || ob.outbound_groups->empty()) {
            throw ConfigError("Urltest outbound '" + ob.tag +
                              "' 'outbound_groups' array must not be empty");
        }
        for (const auto& group : *ob.outbound_groups) {
            if (group.outbounds.empty()) {
                throw ConfigError("Urltest outbound '" + ob.tag +
                                  "' outbound_group has empty 'outbounds' array");
            }
            for (const auto& ref_tag : group.outbounds) {
                bool found = false;
                for (const auto& target : outbounds) {
                    if (target.tag == ref_tag) {
                        found = true;
                        if (target.type != OutboundType::INTERFACE &&
                            target.type != OutboundType::TABLE &&
                            target.type != OutboundType::BLACKHOLE) {
                            throw ConfigError(
                                "Urltest outbound '" + ob.tag +
                                "' references outbound '" + ref_tag +
                                "' which is not an interface, table, or blackhole outbound");
                        }
                        break;
                    }
                }
                if (!found) {
                    throw ConfigError(
                        "Urltest outbound '" + ob.tag +
                        "' references unknown outbound tag '" + ref_tag + "'");
                }
            }
        }
    }

    // Validate: dns.servers[*].detour must reference an existing routable outbound
    if (cfg.dns.has_value()) {
        const auto& dns_servers =
            cfg.dns->servers.value_or(std::vector<DnsServer>{});
        for (const auto& srv : dns_servers) {
            if (!srv.detour.has_value()) continue;
            const std::string& dtag = srv.detour.value();
            bool found = false;
            for (const auto& ob : outbounds) {
                if (ob.tag == dtag) {
                    found = true;
                    if (ob.type == OutboundType::BLACKHOLE ||
                        ob.type == OutboundType::IGNORE) {
                        throw ConfigError(
                            "dns.servers[\"" + srv.tag + "\"].detour: outbound \""
                            + dtag + "\" has no routing table");
                    }
                    break;
                }
            }
            if (!found) {
                throw ConfigError(
                    "dns.servers[\"" + srv.tag + "\"].detour: unknown outbound tag \""
                    + dtag + "\"");
            }
        }
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
