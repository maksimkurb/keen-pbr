#ifdef WITH_API

#include "handler_test_routing.hpp"
#include "../cmd/test_routing_json.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_test_routing_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/routing/test", [&ctx](const std::string& body) -> std::string {
        TestRoutingRequest request;
        try {
            request = parse_test_routing_request(nlohmann::json::parse(body));
        } catch (const nlohmann::json::exception&) {
            nlohmann::json payload = {{"error", "Invalid request body"}};
            throw ApiError("Invalid request body", 400, payload.dump());
        } catch (const TestRoutingRequestError& error) {
            nlohmann::json payload = {{"error", error.what()}};
            throw ApiError(error.what(), 400, payload.dump());
        }

        auto result = ctx.compute_test_routing(request.target, request.criteria);
        return test_routing_result_to_json(result).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
