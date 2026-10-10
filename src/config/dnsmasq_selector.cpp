#include "dnsmasq_selector.hpp"

namespace keen_pbr3 {

namespace {

bool is_alnum(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool all_of_set(std::string_view s, std::string_view extra) {
    for (char c : s) {
        if (!is_alnum(c) && extra.find(c) == std::string_view::npos) return false;
    }
    return true;
}

} // namespace

std::optional<DnsmasqSelector> parse_dnsmasq_selector(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view scheme = text.substr(0, colon);
    const std::string_view id = text.substr(colon + 1);

    bool ok = false;
    if (scheme == "uci") {
        ok = !id.empty() && id.size() <= 64 && all_of_set(id, "_");
    } else if (scheme == "entware") {
        ok = id == "default";
    } else if (scheme == "systemd") {
        constexpr std::string_view suffix = ".service";
        ok = !id.empty() && id.size() <= 128 && all_of_set(id, "@._-") &&
             id.size() >= suffix.size() &&
             id.substr(id.size() - suffix.size()) == suffix;
    } else if (scheme == "test") {
        ok = !id.empty() && id.size() <= 64 && all_of_set(id, "_-");
    }
    if (!ok) return std::nullopt;
    return DnsmasqSelector{std::string(scheme), std::string(id)};
}

} // namespace keen_pbr3
