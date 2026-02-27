#ifdef WITH_API

#include "handlers.hpp"

#include <iomanip>
#include <sstream>

#include <keen-pbr3/version.hpp>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

// Helper to extract the tag from any Outbound variant
std::string outbound_tag(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
}

// Helper to get the type name from an Outbound variant
std::string outbound_type(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, InterfaceOutbound>) return "interface";
        else if constexpr (std::is_same_v<T, TableOutbound>) return "table";
        else if constexpr (std::is_same_v<T, BlackholeOutbound>) return "blackhole";
        else if constexpr (std::is_same_v<T, IgnoreOutbound>) return "ignore";
        else if constexpr (std::is_same_v<T, UrltestOutbound>) return "urltest";
        else return "unknown";
    }, ob);
}

// Format uint32_t as hex string (e.g., "0x00010000")
std::string format_hex(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << val;
    return ss.str();
}

// Build JSON for a single outbound with fwmark info
nlohmann::json outbound_to_json(const Outbound& ob, const OutboundMarkMap& marks) {
    nlohmann::json j;
    std::string tag = outbound_tag(ob);
    j["tag"] = tag;
    j["type"] = outbound_type(ob);

    std::visit([&j](const auto& o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, InterfaceOutbound>) {
            j["interface"] = o.interface;
            if (o.gateway) j["gateway"] = *o.gateway;
        } else if constexpr (std::is_same_v<T, TableOutbound>) {
            j["table_id"] = o.table_id;
        } else if constexpr (std::is_same_v<T, UrltestOutbound>) {
            j["url"] = o.url;
            j["interval_ms"] = o.interval_ms;
        }
    }, ob);

    auto mark_it = marks.find(tag);
    if (mark_it != marks.end()) {
        j["fwmark"] = format_hex(mark_it->second);
    }

    return j;
}

// Convert CircuitState to string
std::string circuit_state_string(CircuitState state) {
    switch (state) {
        case CircuitState::closed: return "closed";
        case CircuitState::open: return "open";
        case CircuitState::half_open: return "half_open";
        default: return "unknown";
    }
}

} // anonymous namespace

void register_api_handlers(ApiServer& server, ApiContext& ctx) {
    // GET /api/status - daemon status, loaded lists, active outbounds, fwmarks, urltest selections
    server.get("/api/status", [&ctx]() -> std::string {
        nlohmann::json j;
        j["version"] = KEEN_PBR3_VERSION_STRING;
        j["status"] = "running";

        const auto& marks = ctx.firewall_state.get_outbound_marks();

        // Outbounds with fwmark assignments
        nlohmann::json outbounds_json = nlohmann::json::array();
        for (const auto& ob : ctx.outbounds) {
            outbounds_json.push_back(outbound_to_json(ob, marks));
        }
        j["outbounds"] = outbounds_json;

        // Loaded lists (from cache metadata + inline config)
        nlohmann::json lists_json = nlohmann::json::object();
        for (const auto& [name, list_cfg] : ctx.lists) {
            nlohmann::json list_j;
            auto meta = ctx.cache_manager.load_metadata(name);
            size_t ips = meta.ips.value_or(0) + list_cfg.ip_cidrs.size();
            size_t cidrs = meta.cidrs.value_or(0);
            size_t domains = meta.domains.value_or(0) + list_cfg.domains.size();
            list_j["ips"] = static_cast<int>(ips);
            list_j["cidrs"] = static_cast<int>(cidrs);
            list_j["domains"] = static_cast<int>(domains);
            lists_json[name] = list_j;
        }
        j["lists"] = lists_json;

        // Current rule-to-outbound mappings
        nlohmann::json rules_json = nlohmann::json::array();
        for (const auto& rule : ctx.firewall_state.get_rules()) {
            nlohmann::json rule_j;
            rule_j["rule_index"] = rule.rule_index;
            rule_j["lists"] = rule.list_names;
            rule_j["outbound"] = rule.outbound_tag;
            switch (rule.action_type) {
                case RuleActionType::Mark:
                    rule_j["action"] = "mark";
                    rule_j["fwmark"] = format_hex(rule.fwmark);
                    break;
                case RuleActionType::Drop:
                    rule_j["action"] = "drop";
                    break;
                case RuleActionType::Skip:
                    rule_j["action"] = "skip";
                    break;
            }
            rule_j["effective_outbound"] = ctx.firewall_state.resolve_effective_outbound(rule);
            rules_json.push_back(rule_j);
        }
        j["rules"] = rules_json;

        // Urltest selections
        const auto& selections = ctx.firewall_state.get_urltest_selections();
        if (!selections.empty()) {
            nlohmann::json sel_json = nlohmann::json::object();
            for (const auto& [ut_tag, child_tag] : selections) {
                sel_json[ut_tag] = child_tag;
            }
            j["urltest_selections"] = sel_json;
        }

        return j.dump();
    });

    // POST /api/reload - trigger list re-download and re-apply
    server.post("/api/reload", [&ctx]() -> std::string {
        ctx.reload_fn();
        nlohmann::json j;
        j["status"] = "ok";
        j["message"] = "Reload triggered";
        return j.dump();
    });

    // GET /api/health - health check results for all outbounds
    server.get("/api/health", [&ctx]() -> std::string {
        nlohmann::json j;
        nlohmann::json results = nlohmann::json::array();

        for (const auto& ob : ctx.outbounds) {
            nlohmann::json entry;
            std::string tag = outbound_tag(ob);
            entry["tag"] = tag;
            entry["type"] = outbound_type(ob);

            if (std::holds_alternative<UrltestOutbound>(ob)) {
                // Report urltest state: per-child latencies, circuit breaker states, selected outbound
                try {
                    const auto& state = ctx.urltest_manager.get_state(tag);
                    entry["selected_outbound"] = state.selected_outbound;

                    nlohmann::json children = nlohmann::json::array();
                    for (const auto& [child_tag, result] : state.last_results) {
                        nlohmann::json child_j;
                        child_j["tag"] = child_tag;
                        child_j["success"] = result.success;
                        child_j["latency_ms"] = result.latency_ms;
                        if (!result.error.empty()) {
                            child_j["error"] = result.error;
                        }

                        auto cb_it = state.circuit_breakers.find(child_tag);
                        if (cb_it != state.circuit_breakers.end()) {
                            child_j["circuit_breaker"] = circuit_state_string(
                                cb_it->second.state(child_tag));
                        }

                        children.push_back(child_j);
                    }
                    entry["children"] = children;
                    entry["status"] = state.selected_outbound.empty() ? "degraded" : "healthy";
                } catch (const std::out_of_range&) {
                    entry["status"] = "unknown";
                }
            } else {
                // Interface/table outbounds are always considered healthy
                entry["status"] = "healthy";
            }

            results.push_back(entry);
        }

        j["outbounds"] = results;
        return j.dump();
    });

    // GET /api/health/routing - verify live routing and firewall state against expected config.
    // RoutingHealthChecker::check() catches all internal exceptions; if it still throws,
    // the server wrapper returns HTTP 500. The JSON body contains "overall":"ok"/"degraded"/"error".
    server.get("/api/health/routing", [&ctx]() -> std::string {
        try {
            auto report = ctx.routing_health_checker.check();
            return routing_health_report_to_json(report).dump();
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["overall"] = "error";
            err["error"] = e.what();
            // Re-throw so the server wrapper sets HTTP 500
            throw std::runtime_error(err.dump());
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
