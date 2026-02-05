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
    if (j.contains("list_update_interval")) {
        cfg.list_update_interval =
            parse_duration(j.at("list_update_interval").get<std::string>());
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

    if (j.contains("outbounds")) {
        for (const auto& ob_json : j.at("outbounds")) {
            config.outbounds.push_back(parse_outbound(ob_json));
        }
    }

    return config;
}

} // namespace keen_pbr3
