#ifdef WITH_API

#include "handlers.hpp"
#include "handler_health_service.hpp"
#include "handler_reload.hpp"
#include "handler_config.hpp"
#include "handler_health_routing.hpp"
#include "handler_runtime_outbounds.hpp"
#include "handler_test_routing.hpp"
#include "handler_dns_test.hpp"

#include <httplib.h>

namespace keen_pbr3 {

namespace {

std::string decode_base64(const std::string& input) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    int val = 0;
    int bits = -8;
    for (unsigned char c : input) {
        if (c == '=') {
            break;
        }
        const auto idx = chars.find(static_cast<char>(c));
        if (idx == std::string::npos) {
            return {};
        }

        val = (val << 6) + static_cast<int>(idx);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return output;
}

bool request_is_authenticated(const httplib::Request& req,
                              const ApiCredentials& creds) {
    const auto auth_header = req.get_header_value("Authorization");
    if (auth_header.rfind("Basic ", 0) != 0) {
        return false;
    }

    const std::string decoded = decode_base64(auth_header.substr(6));
    const auto colon = decoded.find(':');
    if (colon == std::string::npos) {
        return false;
    }

    const std::string username = decoded.substr(0, colon);
    const std::string password = decoded.substr(colon + 1);
    if (username != creds.username) {
        return false;
    }

    return bcrypt_verify_password(password, creds.password_hash);
}

} // namespace

void register_api_handlers(ApiServer& server, ApiContext& ctx) {
    server.set_request_guard([&ctx](const httplib::Request& req, const std::string& path) {
        (void)path;

        const auto creds = ctx.api_credentials_fn();
        if (!creds.has_value()) {
            throw ApiError(
                "API password is not configured. Set username/password using POST /api/auth/config.",
                403,
                R"({"error":"API password is not configured. Set username/password using POST /api/auth/config."})");
        }

        if (!request_is_authenticated(req, *creds)) {
            throw ApiError("Forbidden", 403, R"({"error":"Forbidden"})");
        }
    });

    register_health_service_handler(server, ctx);
    register_reload_handler(server, ctx);
    register_config_handler(server, ctx);
    register_health_routing_handler(server, ctx);
    register_runtime_outbounds_handler(server, ctx);
    register_test_routing_handler(server, ctx);
    register_dns_test_handler(server, ctx);
}

} // namespace keen_pbr3

#endif // WITH_API
