#pragma once

#ifdef WITH_API

#include "../config/config.hpp"

#include <optional>
#include <string>

namespace keen_pbr3 {

struct ApiCredentials {
    std::string username;
    std::string password_hash;
};

bool api_password_is_configured(const Config& config);
std::optional<ApiCredentials> api_credentials_from_config(const Config& config);

Config redact_api_credentials(const Config& config);

bool bcrypt_hash_password(const std::string& password,
                          std::string& hash_out,
                          std::string& error_out);

bool bcrypt_verify_password(const std::string& password,
                            const std::string& bcrypt_hash);

} // namespace keen_pbr3

#endif // WITH_API
