#include "config.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

using json = nlohmann::json;

// Parse duration strings like "30s", "5m", "24h"
static std::chrono::seconds parse_duration(const std::string& str) {
    if (str.empty()) {
        throw ConfigError("Empty duration string");
    }

    char suffix = str.back();
    std::string num_str = str.substr(0, str.size() - 1);

    long long value;
    try {
        value = std::stoll(num_str);
    } catch (const std::exception&) {
        throw ConfigError("Invalid duration: " + str);
    }

    if (value < 0) {
        throw ConfigError("Negative duration: " + str);
    }

    switch (suffix) {
    case 's':
        return std::chrono::seconds(value);
    case 'm':
        return std::chrono::seconds(value * 60);
    case 'h':
        return std::chrono::seconds(value * 3600);
    default:
        throw ConfigError("Unknown duration suffix '" + std::string(1, suffix) +
                          "' in: " + str);
    }
}

static DaemonConfig parse_daemon(const json& j) {
    DaemonConfig cfg;
    if (j.contains("pid_file")) {
        cfg.pid_file = j.at("pid_file").get<std::string>();
    }
    if (j.contains("cache_dir")) {
        cfg.cache_dir = j.at("cache_dir").get<std::string>();
    }
    return cfg;
}

static Outbound parse_outbound(const json& j) {
    if (!j.contains("type")) {
        throw ConfigError("Outbound missing 'type' field");
    }
    if (!j.contains("tag")) {
        throw ConfigError("Outbound missing 'tag' field");
    }

    std::string type = j.at("type").get<std::string>();
    std::string tag = j.at("tag").get<std::string>();

    if (type == "interface") {
        if (!j.contains("interface")) {
            throw ConfigError("Interface outbound '" + tag +
                              "' missing 'interface' field");
        }
        InterfaceOutbound ob;
        ob.tag = tag;
        ob.interface = j.at("interface").get<std::string>();
        if (j.contains("gateway")) {
            ob.gateway = j.at("gateway").get<std::string>();
        }
        if (j.contains("ping_target")) {
            ob.ping_target = j.at("ping_target").get<std::string>();
        }
        if (j.contains("ping_interval")) {
            ob.ping_interval =
                parse_duration(j.at("ping_interval").get<std::string>());
        }
        if (j.contains("ping_timeout")) {
            ob.ping_timeout =
                parse_duration(j.at("ping_timeout").get<std::string>());
        }
        return ob;
    } else if (type == "table") {
        if (!j.contains("table")) {
            throw ConfigError("Table outbound '" + tag +
                              "' missing 'table' field");
        }
        TableOutbound ob;
        ob.tag = tag;
        ob.table_id = j.at("table").get<uint32_t>();
        return ob;
    } else if (type == "blackhole") {
        BlackholeOutbound ob;
        ob.tag = tag;
        return ob;
    } else {
        throw ConfigError("Unknown outbound type: " + type);
    }
}

static ApiConfig parse_api(const json& j) {
    ApiConfig cfg;
    if (j.contains("enabled")) {
        cfg.enabled = j.at("enabled").get<bool>();
    }
    if (j.contains("listen")) {
        cfg.listen = j.at("listen").get<std::string>();
    }
    return cfg;
}

static ListConfig parse_list(const std::string& name, const json& j) {
    ListConfig cfg;
    if (j.contains("url")) {
        cfg.url = j.at("url").get<std::string>();
    }
    if (j.contains("domains")) {
        for (const auto& d : j.at("domains")) {
            cfg.domains.push_back(d.get<std::string>());
        }
    }
    if (j.contains("ip_cidrs")) {
        for (const auto& c : j.at("ip_cidrs")) {
            cfg.ip_cidrs.push_back(c.get<std::string>());
        }
    }
    if (j.contains("file")) {
        cfg.file = j.at("file").get<std::string>();
    }
    if (j.contains("ttl")) {
        auto ttl_val = j.at("ttl");
        if (ttl_val.is_string()) {
            auto dur = parse_duration(ttl_val.get<std::string>());
            cfg.ttl = static_cast<uint32_t>(dur.count());
        } else {
            cfg.ttl = ttl_val.get<uint32_t>();
        }
    }
    // At least one source must be specified
    if (!cfg.url && cfg.domains.empty() && cfg.ip_cidrs.empty() && !cfg.file) {
        throw ConfigError("List '" + name +
                          "' must have at least one of: url, domains, ip_cidrs, file");
    }
    return cfg;
}

static RouteRule parse_route_rule(const json& j) {
    RouteRule rule;

    if (!j.contains("list")) {
        throw ConfigError("Route rule missing 'list' field");
    }
    for (const auto& l : j.at("list")) {
        rule.lists.push_back(l.get<std::string>());
    }

    if (j.contains("action")) {
        std::string action_str = j.at("action").get<std::string>();
        if (action_str == "skip") {
            rule.action = SkipAction{};
        } else {
            throw ConfigError("Unknown route action: " + action_str);
        }
    } else if (j.contains("outbounds")) {
        std::vector<std::string> chain;
        for (const auto& o : j.at("outbounds")) {
            chain.push_back(o.get<std::string>());
        }
        if (chain.empty()) {
            throw ConfigError("Route rule 'outbounds' array must not be empty");
        }
        rule.action = std::move(chain);
    } else if (j.contains("outbound")) {
        rule.action = j.at("outbound").get<std::string>();
    } else {
        throw ConfigError(
            "Route rule must have 'outbound', 'outbounds', or 'action' field");
    }

    return rule;
}

static RouteConfig parse_route(const json& j) {
    RouteConfig cfg;
    if (j.contains("rules")) {
        for (const auto& r : j.at("rules")) {
            cfg.rules.push_back(parse_route_rule(r));
        }
    }
    if (j.contains("fallback")) {
        cfg.fallback = j.at("fallback").get<std::string>();
    }
    return cfg;
}

static DnsServer parse_dns_server(const json& j) {
    DnsServer srv;
    if (!j.contains("tag")) {
        throw ConfigError("DNS server missing 'tag' field");
    }
    if (!j.contains("address")) {
        throw ConfigError("DNS server missing 'address' field");
    }
    srv.tag = j.at("tag").get<std::string>();
    srv.address = j.at("address").get<std::string>();
    if (j.contains("detour")) {
        srv.detour = j.at("detour").get<std::string>();
    }
    return srv;
}

static DnsRule parse_dns_rule(const json& j) {
    DnsRule rule;
    if (!j.contains("list")) {
        throw ConfigError("DNS rule missing 'list' field");
    }
    if (!j.contains("server")) {
        throw ConfigError("DNS rule missing 'server' field");
    }
    for (const auto& l : j.at("list")) {
        rule.lists.push_back(l.get<std::string>());
    }
    rule.server = j.at("server").get<std::string>();
    return rule;
}

static DnsConfig parse_dns(const json& j) {
    DnsConfig cfg;
    if (j.contains("servers")) {
        for (const auto& s : j.at("servers")) {
            cfg.servers.push_back(parse_dns_server(s));
        }
    }
    if (j.contains("rules")) {
        for (const auto& r : j.at("rules")) {
            cfg.rules.push_back(parse_dns_rule(r));
        }
    }
    if (j.contains("fallback")) {
        cfg.fallback = j.at("fallback").get<std::string>();
    }
    return cfg;
}

Config parse_config(const std::string& json_str) {
    json j;
    try {
        j = json::parse(json_str);
    } catch (const json::parse_error& e) {
        throw ConfigError(std::string("Invalid JSON: ") + e.what());
    }

    Config config;

    if (j.contains("daemon")) {
        config.daemon = parse_daemon(j.at("daemon"));
    }

    if (j.contains("api")) {
        config.api = parse_api(j.at("api"));
    }

    if (j.contains("outbounds")) {
        for (const auto& ob_json : j.at("outbounds")) {
            config.outbounds.push_back(parse_outbound(ob_json));
        }
    }

    if (j.contains("dns")) {
        config.dns = parse_dns(j.at("dns"));
    }

    if (j.contains("route")) {
        config.route = parse_route(j.at("route"));
    }

    if (j.contains("lists")) {
        for (const auto& [name, list_json] : j.at("lists").items()) {
            config.lists[name] = parse_list(name, list_json);
        }
    }

    return config;
}

} // namespace keen_pbr3
