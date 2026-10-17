#include "doh_support.hpp"

#include <filesystem>
#include <system_error>

namespace keen_pbr3 {

std::optional<CaBundle> find_ca_bundle(const std::vector<std::string>& files,
                                       const std::vector<std::string>& dirs) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& file : files) {
        if (fs::is_regular_file(file, ec) && fs::file_size(file, ec) > 0) {
            return CaBundle{file, false};
        }
    }
    for (const auto& dir : dirs) {
        if (fs::is_directory(dir, ec) && fs::directory_iterator(dir, ec) != fs::directory_iterator()) {
            return CaBundle{dir, true};
        }
    }
    return std::nullopt;
}

std::optional<CaBundle> detect_ca_bundle() {
    return find_ca_bundle(
        {
            "/opt/etc/ssl/certs/ca-certificates.crt",  // Entware
            "/opt/etc/ssl/cert.pem",
            "/etc/ssl/certs/ca-certificates.crt",      // Debian, OpenWrt (ca-bundle)
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/cert.pem",
        },
        {
            "/opt/etc/ssl/certs",   // Entware
            "/etc/ssl/certshashed", // Keenetic firmware
            "/etc/ssl/certs",
        });
}

} // namespace keen_pbr3
