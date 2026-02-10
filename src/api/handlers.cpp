#ifdef WITH_API

#include "handlers.hpp"

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
        else return "blackhole";
    }, ob);
}

// Build JSON for a single outbound
nlohmann::json outbound_to_json(const Outbound& ob) {
    nlohmann::json j;
    j["tag"] = outbound_tag(ob);
    j["type"] = outbound_type(ob);

    std::visit([&j](const auto& o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, InterfaceOutbound>) {
            j["interface"] = o.interface;
            if (o.gateway) j["gateway"] = *o.gateway;
            if (o.ping_target) j["ping_target"] = *o.ping_target;
        } else if constexpr (std::is_same_v<T, TableOutbound>) {
            j["table_id"] = o.table_id;
        }
    }, ob);

    return j;
}

std::string health_status_string(HealthStatus s) {
    switch (s) {
        case HealthStatus::healthy: return "healthy";
        case HealthStatus::unhealthy: return "unhealthy";
        default: return "unknown";
    }
}

} // anonymous namespace

void register_api_handlers(ApiServer& server, ApiContext& ctx) {
    // GET /api/status - daemon status, loaded lists, active outbounds
    server.get("/api/status", [&ctx]() -> std::string {
        nlohmann::json j;
        j["version"] = KEEN_PBR3_VERSION_STRING;
        j["status"] = "running";

        // Outbounds
        nlohmann::json outbounds_json = nlohmann::json::array();
        for (const auto& ob : ctx.outbounds) {
            outbounds_json.push_back(outbound_to_json(ob));
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
            std::string tag = outbound_tag(ob);
            nlohmann::json entry;
            entry["tag"] = tag;
            entry["type"] = outbound_type(ob);

            if (ctx.health_checker.has_target(tag)) {
                entry["monitored"] = true;
                entry["status"] = health_status_string(
                    ctx.health_checker.last_status(tag));
            } else {
                entry["monitored"] = false;
                entry["status"] = "healthy"; // unmonitored = always healthy
            }

            results.push_back(entry);
        }

        j["outbounds"] = results;
        return j.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
