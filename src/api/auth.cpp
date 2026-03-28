#ifdef WITH_API

#include "auth.hpp"

#include <array>
#include <bcrypt.h>

namespace keen_pbr3 {

bool api_password_is_configured(const Config& config) {
    const auto creds = api_credentials_from_config(config);
    return creds.has_value() && !creds->password_hash.empty();
}

std::optional<ApiCredentials> api_credentials_from_config(const Config& config) {
    if (!config.api) {
        return std::nullopt;
    }

    const std::string username = config.api->username.value_or("");
    const std::string password_hash = config.api->password.value_or("");
    if (username.empty() || password_hash.empty()) {
        return std::nullopt;
    }

    return ApiCredentials{username, password_hash};
}

Config redact_api_credentials(const Config& config) {
    Config copy = config;
    if (copy.api && copy.api->password.has_value()) {
        copy.api->password = "[REDACTED]";
    }
    return copy;
}

bool bcrypt_hash_password(const std::string& password,
                          std::string& hash_out,
                          std::string& error_out) {
    error_out.clear();
    hash_out.clear();

    if (password.empty()) {
        error_out = "Password must not be empty";
        return false;
    }

    std::array<char, BCRYPT_HASHSIZE> salt{};
    std::array<char, BCRYPT_HASHSIZE> hash{};

    if (bcrypt_gensalt(12, salt.data()) != 0) {
        error_out = "Failed to generate bcrypt salt";
        return false;
    }

    if (bcrypt_hashpw(password.c_str(), salt.data(), hash.data()) != 0) {
        error_out = "Failed to generate bcrypt hash";
        return false;
    }

    hash_out = hash.data();
    return true;
}

bool bcrypt_verify_password(const std::string& password,
                            const std::string& bcrypt_hash) {
    if (password.empty() || bcrypt_hash.empty()) {
        return false;
    }

    return bcrypt_checkpw(password.c_str(), bcrypt_hash.c_str()) == 0;
}

} // namespace keen_pbr3

#endif // WITH_API
