#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace keen_pbr3 {

// Persistent, platform-specific identity of one dnsmasq instance.
//   uci:<section>          OpenWrt, section name as printed by `uci -X show dhcp`
//   entware:default        Keenetic/Entware (only one instance exists)
//   systemd:<unit>.service Debian
//   test:<name>            netns test harness only
struct DnsmasqSelector {
    std::string scheme;  // "uci" | "entware" | "systemd" | "test"
    std::string id;

    std::string str() const { return scheme + ":" + id; }
    bool operator==(const DnsmasqSelector& o) const {
        return scheme == o.scheme && id == o.id;
    }
    bool operator!=(const DnsmasqSelector& o) const { return !(*this == o); }
};

// Strict parser. Rules (whole string must match):
//   uci:     id matches [A-Za-z0-9_]{1,64}
//   entware: id == "default"
//   systemd: id matches [A-Za-z0-9@._-]{1,128} and ends with ".service"
//   test:    id matches [A-Za-z0-9_-]{1,64}
// Anything else -> std::nullopt.
std::optional<DnsmasqSelector> parse_dnsmasq_selector(std::string_view text);

} // namespace keen_pbr3
