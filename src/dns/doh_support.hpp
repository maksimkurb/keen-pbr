#pragma once

#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// DNS-over-HTTPS is compiled in unless the build says WITH_DOH=OFF.
#ifdef WITH_DOH
constexpr bool kDohBuiltIn = true;
#else
constexpr bool kDohBuiltIn = false;
#endif

// The trust anchors DoH connections are verified against.
struct CaBundle {
    std::string path;
    bool is_directory{false};
};

// First existing certificate file of `files`, else the first non-empty
// directory of `dirs`. Looks at the file system, so call it once at start.
std::optional<CaBundle> find_ca_bundle(const std::vector<std::string>& files,
                                       const std::vector<std::string>& dirs);

// find_ca_bundle() over the places the supported systems keep their
// certificates (Entware, Keenetic firmware, OpenWrt, Debian).
std::optional<CaBundle> detect_ca_bundle();

} // namespace keen_pbr3
