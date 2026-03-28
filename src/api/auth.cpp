#ifdef WITH_API

#include "auth.hpp"

#include <array>
#include <crypt.h>
#include <random>

namespace keen_pbr3 {

namespace {

constexpr std::array<char, 64> kBcryptAlphabet = {
    '.', '/',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
    'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

std::string generate_bcrypt_salt() {
    std::random_device rd;
    std::uniform_int_distribution<size_t> dist(0, kBcryptAlphabet.size() - 1);

    std::string salt = "$2b$12$";
    salt.reserve(29);
    for (size_t i = 0; i < 22; ++i) {
        salt.push_back(kBcryptAlphabet[dist(rd)]);
    }
    return salt;
}

bool looks_like_bcrypt_hash(const std::string& hash) {
    return hash.size() >= 60 && hash.rfind("$2", 0) == 0;
}

} // namespace

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

    const std::string salt = generate_bcrypt_salt();
    struct crypt_data data {};
    data.initialized = 0;
    const char* generated = crypt_r(password.c_str(), salt.c_str(), &data);
    if (generated == nullptr) {
        error_out = "Failed to generate bcrypt hash";
        return false;
    }

    hash_out = generated;
    if (!looks_like_bcrypt_hash(hash_out)) {
        error_out = "Generated hash is not bcrypt";
        hash_out.clear();
        return false;
    }

    return true;
}

bool bcrypt_verify_password(const std::string& password,
                            const std::string& bcrypt_hash) {
    if (password.empty() || bcrypt_hash.empty()) {
        return false;
    }

    struct crypt_data data {};
    data.initialized = 0;
    const char* generated = crypt_r(password.c_str(), bcrypt_hash.c_str(), &data);
    if (generated == nullptr) {
        return false;
    }

    return bcrypt_hash == generated;
}

} // namespace keen_pbr3

#endif // WITH_API
