#ifdef WITH_API

#include "handler_dns_check.hpp"
#include "../dns/dns_check.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

[[noreturn]] void bad_request(const std::string& message) {
    nlohmann::json payload = {{"error", message}};
    throw ApiError(message, 400, payload.dump());
}

} // namespace

void register_dns_check_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/dns/servers/check", [&ctx](const std::string& body) -> std::string {
        DnsCheckRequest request;
        try {
            request = parse_dns_check_request(body.empty() ? nlohmann::json::object()
                                                           : nlohmann::json::parse(body));
        } catch (const nlohmann::json::exception&) {
            bad_request("Invalid request body");
        } catch (const DnsCheckRequestError& error) {
            bad_request(error.what());
        }

        if (!ctx.check_dns_servers_fn) {
            throw ApiError("DNS server check is unavailable", 503);
        }
        try {
            return dns_check_report_to_json(ctx.check_dns_servers_fn(request)).dump();
        } catch (const DnsCheckRequestError& error) {
            bad_request(error.what());
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
