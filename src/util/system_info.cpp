#include "system_info.hpp"

#include "../http/http_client.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

namespace keen_pbr3 {

namespace {

#ifndef KEEN_PBR_TARGET_OS
#define KEEN_PBR_TARGET_OS "linux"
#endif

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
                value.end());
    return value;
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::map<std::string, std::string> parse_shell_kv_file(const std::filesystem::path& path) {
    std::map<std::string, std::string> values;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, equals));
        std::string value = unquote(line.substr(equals + 1));
        values[std::move(key)] = std::move(value);
    }
    return values;
}

std::optional<std::string> map_lookup(const std::map<std::string, std::string>& values,
                                      const std::string& key) {
    const auto it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        return std::nullopt;
    }
    return it->second;
}

std::string value_or_unknown(const std::optional<std::string>& first,
                             const std::optional<std::string>& second = std::nullopt,
                             const std::optional<std::string>& third = std::nullopt) {
    if (first && !first->empty()) {
        return *first;
    }
    if (second && !second->empty()) {
        return *second;
    }
    if (third && !third->empty()) {
        return *third;
    }
    return "unknown";
}

std::optional<std::string> detect_keenetic_version() {
    try {
        HttpClient client;
        client.set_timeout(std::chrono::seconds(2));
        client.set_max_response_size(128);
        const std::string response = trim(
            client.download("http://127.0.0.1:79/rci/show/version/title"));
        if (response.empty()) {
            return std::nullopt;
        }
        return unquote(response);
    } catch (const HttpError&) {
        return std::nullopt;
    }
}

bool path_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

} // namespace

SystemInfo detect_system_info() {
    SystemInfo info;
    info.os_type = KEEN_PBR_TARGET_OS;

    const auto openwrt_release_path = std::filesystem::path("/etc/openwrt_release");
    const auto os_release_path = std::filesystem::path("/etc/os-release");

    if (info.os_type == "openwrt" && path_exists(openwrt_release_path)) {
        const auto values = parse_shell_kv_file(openwrt_release_path);
        info.os_version = map_lookup(values, "DISTRIB_RELEASE").value_or("unknown");
        info.build_variant = map_lookup(values, "DISTRIB_TARGET").value_or("unknown");
        return info;
    }

    const auto os_release_values =
        path_exists(os_release_path) ? parse_shell_kv_file(os_release_path)
                                     : std::map<std::string, std::string>{};

    if (info.os_type == "keenetic") {
        info.os_version = value_or_unknown(detect_keenetic_version(),
                                           map_lookup(os_release_values, "VERSION_ID"),
                                           map_lookup(os_release_values, "VERSION"));
        info.build_variant = "keenetic";
        return info;
    }

    info.os_version = value_or_unknown(map_lookup(os_release_values, "VERSION_ID"),
                                       map_lookup(os_release_values, "VERSION"));
    info.build_variant = info.os_type;

    return info;
}

const SystemInfo& cached_system_info() {
    static const SystemInfo info = detect_system_info();
    return info;
}

} // namespace keen_pbr3
