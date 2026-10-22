#pragma once

#include "../api/generated/api_types.hpp"
#include "../daemon/dnsmasq_manager.hpp"
#include "../runtime/lifecycle_operation.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace keen_pbr3 {

struct ServiceHealthState {
    api::HealthResponseStatus status{api::HealthResponseStatus::STOPPED};
    std::string runtime_state;
    std::string runtime_state_reason;
    std::string os_type{"unknown"};
    std::string os_version{"unknown"};
    std::string build_variant{"unknown"};
    std::optional<std::int64_t> apply_started_ts;
    bool config_is_draft{false};
    bool rollback_available{false};
    std::optional<LifecycleOperationSnapshot> lifecycle_operation;
    std::optional<api::InterceptHealth> intercept;
    std::optional<api::DnsmasqHealth> dnsmasq;
};

api::HealthResponse build_health_response(const ServiceHealthState& state);

api::DnsmasqHealth to_dnsmasq_health(const DnsmasqStatus& status);

} // namespace keen_pbr3
