#ifdef WITH_API

#include "handler_lists_refresh.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

std::optional<std::string> parse_requested_list_name(const std::string& body) {
    if (body.empty()) {
        return std::nullopt;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(body);
    } catch (const nlohmann::json::exception&) {
        throw ApiError("Invalid request body", 400);
    }
    if (payload.is_null()) {
        return std::nullopt;
    }
    if (!payload.is_object()) {
        throw ApiError("Invalid request body", 400);
    }

    const auto it = payload.find("name");
    if (it == payload.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        throw ApiError("Field 'name' must be a string", 400);
    }

    return it->get<std::string>();
}

} // namespace

void register_lists_refresh_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/lists/refresh", [&ctx](const std::string& body) -> std::string {
        const auto requested_name = parse_requested_list_name(body);
        const auto result = ctx.refresh_lists(requested_name);

        api::ListRefreshResponse response;
        response.status = api::ConfigUpdateResponseStatus::OK;
        response.message = result.message;
        response.refreshed_lists = result.refreshed_lists;
        response.changed_lists = result.changed_lists;
        response.failed_lists = result.failed_lists;
        response.reloaded = result.reloaded;
        return nlohmann::json(response).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
