// API response types for keen-pbr3.
// Hand-written from docs/openapi.yaml (QuickType cannot parse OpenAPI 3.1 with
// discriminator/oneOf/additionalProperties). Regenerate by running:
//   make generate   (requires Node.js; rewrites this file via scripts/generate_api_types.sh)

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {
namespace api {

// ── /api/health/service ───────────────────────────────────────────────────────

struct HealthChild {
    std::string tag;
    bool success{false};
    int64_t latency_ms{0};
    std::optional<std::string> error;
    std::optional<std::string> circuit_breaker;
};

inline void to_json(nlohmann::json& j, const HealthChild& c) {
    j = {{"tag", c.tag}, {"success", c.success}, {"latency_ms", c.latency_ms}};
    if (c.error) j["error"] = *c.error;
    if (c.circuit_breaker) j["circuit_breaker"] = *c.circuit_breaker;
}

struct HealthEntry {
    std::string tag;
    std::string type;
    std::string status;
    std::optional<std::string> selected_outbound;
    std::optional<std::vector<HealthChild>> children;
};

inline void to_json(nlohmann::json& j, const HealthEntry& e) {
    j = {{"tag", e.tag}, {"type", e.type}, {"status", e.status}};
    if (e.selected_outbound) j["selected_outbound"] = *e.selected_outbound;
    if (e.children) j["children"] = *e.children;
}

struct HealthResponse {
    std::string version;
    std::string status;
    std::vector<HealthEntry> outbounds;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HealthResponse, version, status, outbounds)

// ── /api/reload ───────────────────────────────────────────────────────────────

struct ReloadResponse {
    std::string status;
    std::string message;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ReloadResponse, status, message)

// ── /api/config ───────────────────────────────────────────────────────────────

struct ConfigUpdateResponse {
    std::string status;
    std::string message;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConfigUpdateResponse, status, message)

struct ErrorResponse {
    std::string error;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ErrorResponse, error)

// ── /api/health/routing (error case) ─────────────────────────────────────────

struct RoutingHealthErrorResponse {
    std::string overall{"error"};
    std::string error;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoutingHealthErrorResponse, overall, error)

} // namespace api
} // namespace keen_pbr3
