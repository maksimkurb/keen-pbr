#include "config.hpp"

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

    // Validate: each list must have at least one source
    for (const auto& [name, list_cfg] : cfg.lists.value_or(std::map<std::string, ListConfig>{})) {
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
