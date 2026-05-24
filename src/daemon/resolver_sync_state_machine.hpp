#pragma once

#include "../api/generated/api_types.hpp"
#include "../dns/dns_txt_client.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace keen_pbr3 {

struct ResolverSyncSnapshot {
    std::string expected_hash;
    std::string actual_hash;
    std::optional<std::int64_t> actual_ts;
    std::optional<std::int64_t> last_probe_ts;
    std::optional<std::int64_t> apply_started_ts;
    std::optional<api::ResolverConfigSyncState> sync_state;
    api::ResolverConfigProbeStatus probe_status{api::ResolverConfigProbeStatus::UNKNOWN};
    api::ResolverLiveStatus live_status{api::ResolverLiveStatus::UNKNOWN};
};

class ResolverSyncStateMachine {
public:
    static constexpr std::int64_t kConvergingWindowSeconds = 15;

    void runtime_stopped();
    void resolver_not_configured();
    void expected_hash_updated(std::string expected_hash);
    void apply_started(std::int64_t ts, std::string expected_hash);
    void probe_succeeded(std::string actual_hash,
                         std::optional<std::int64_t> actual_ts,
                         std::optional<std::int64_t> probe_ts);
    void probe_failed(ResolverConfigHashProbeStatus status,
                      std::optional<std::int64_t> probe_ts);

    ResolverSyncSnapshot snapshot(std::int64_t now_ts) const;

private:
    std::string expected_hash_;
    std::string actual_hash_;
    std::optional<std::int64_t> actual_ts_;
    std::optional<std::int64_t> last_probe_ts_;
    std::optional<std::int64_t> apply_started_ts_;
    api::ResolverConfigProbeStatus probe_status_{api::ResolverConfigProbeStatus::UNKNOWN};
    bool runtime_active_{true};
    bool resolver_configured_{true};
};

api::ResolverConfigProbeStatus resolver_probe_status_from_hash_probe_status(
    ResolverConfigHashProbeStatus status);

} // namespace keen_pbr3
