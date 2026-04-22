#include "system_info.hpp"

#include "../http/http_client.hpp"

#include <nlohmann/json.hpp>

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

bool path_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

#ifdef KEEN_PBR3_TESTING
std::optional<SystemInfo>& system_info_override_for_tests() {
    static std::optional<SystemInfo> override;
    return override;
}
#endif

} // namespace

std::optional<std::string> parse_keenetic_version_from_rci_response(
    const std::string& response) {
    const std::string trimmed = trim(response);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (trimmed.front() != '{' && trimmed.front() != '[') {
        const std::string value = unquote(trimmed);
        return value.empty() ? std::nullopt : std::optional<std::string>(value);
    }

    try {
        const auto doc = nlohmann::json::parse(trimmed);
        if (doc.is_string()) {
            const std::string value = trim(doc.get<std::string>());
            return value.empty() ? std::nullopt : std::optional<std::string>(value);
        }
        if (!doc.is_object()) {
            return std::nullopt;
        }

        for (const char* key : {"title", "release", "version"}) {
            const auto it = doc.find(key);
            if (it == doc.end() || !it->is_string()) {
                continue;
            }

            const std::string value = trim(it->get<std::string>());
            if (!value.empty()) {
                return value;
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<int> parse_keenetic_major_version(const std::string& version) {
    const std::string normalized = trim(version);
    if (normalized.empty()) {
        return std::nullopt;
    }

    size_t end = 0;
    while (end < normalized.size() &&
           std::isdigit(static_cast<unsigned char>(normalized[end]))) {
        ++end;
    }
    if (end == 0) {
        return std::nullopt;
    }

    try {
        return std::stoi(normalized.substr(0, end));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool keenetic_version_supports_encrypted_dns(const std::string& version) {
    const auto major = parse_keenetic_major_version(version);
    return major.has_value() && *major >= 3;
}

std::optional<std::string> detect_keenetic_version() {
    try {
        HttpClient client;
        client.set_timeout(std::chrono::seconds(2));
        client.set_max_response_size(2048);
        return parse_keenetic_version_from_rci_response(
            client.download("http://127.0.0.1:79/rci/show/version"));
    } catch (const HttpError&) {
        return std::nullopt;
    }
}

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
#ifdef KEEN_PBR3_TESTING
    if (system_info_override_for_tests().has_value()) {
        return *system_info_override_for_tests();
    }
#endif

    static const SystemInfo info = detect_system_info();
    return info;
}

#ifdef KEEN_PBR3_TESTING
void set_system_info_for_tests(std::optional<SystemInfo> info) {
    system_info_override_for_tests() = std::move(info);
}

void reset_system_info_for_tests() {
    system_info_override_for_tests().reset();
}
#endif

} // namespace keen_pbr3
