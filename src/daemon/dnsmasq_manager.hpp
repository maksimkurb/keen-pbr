#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dns_txt_probe.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/safe_exec.hpp"
#include "../util/traced_mutex.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

enum class DnsmasqSyncState {
    Disabled,
    Ok,
    Applying,
    // Out of sync; an automatic repair is scheduled (DnsmasqStatus::next_repair_ts).
    Reconciling,
    Error,
};

// Answer of the hook `alive` (service manager view, independent of DNS).
enum class DnsmasqLiveness {
    Alive,
    Dead,
    Unknown,
};

// The single gate for every dnsmasq interaction (hook calls, probes, the
// periodic check): keen-pbr touches dnsmasq only while the DNS rules module is
// switched to dnsmasq.
inline bool dnsmasq_integration_enabled(const Config& config) {
    return effective_resolver_integration(config) == ResolverIntegrationMode::DNSMASQ;
}

// What the last config-hash.keen.pbr TXT probe showed.
enum class DnsmasqProbeState {
    NotChecked,
    Ok,           // TXT present and parsed
    Missing,      // dnsmasq runs without the keen-pbr config
    Invalid,      // TXT present but not a keen-pbr stamp
    QueryFailed,  // dnsmasq did not answer
};

// Snapshot of the generated dnsmasq config state, exposed through health.
struct DnsmasqStatus {
    ResolverIntegrationMode mode{ResolverIntegrationMode::NONE};
    DnsmasqSyncState state{DnsmasqSyncState::Disabled};
    // MD5 of the config keen-pbr generated and expects dnsmasq to serve.
    std::string config_hash;
    // Ok means dnsmasq confirmed (via the TXT stamp) that it serves config_hash.
    DnsmasqProbeState probe_state{DnsmasqProbeState::NotChecked};
    // What dnsmasq reported in the stamp (unset while it reports none).
    std::string loaded_hash;
    std::optional<std::int64_t> loaded_boottime_ms;
    std::optional<std::int64_t> loaded_ts;
    std::optional<std::int64_t> last_check_ts;
    // Last dnsmasq reload that keen-pbr did not cause (restart by the user/OS).
    std::optional<std::int64_t> last_external_reload_ts;
    std::optional<std::int64_t> last_apply_ts;
    std::string last_error;
    // Automatic repairs used in the current budget (0 when none) and the limit.
    int repair_attempt{0};
    int repair_max_attempts{0};
    // Why the last/current repair was needed (human sentence).
    std::string repair_reason;
    // Unix seconds when the next automatic repair may run (only Reconciling).
    std::optional<std::int64_t> next_repair_ts;
    // Attempts exhausted: no more automatic restarts until an explicit apply.
    bool repair_paused{false};
    // Last answer of the hook `alive` (unset if never asked).
    std::optional<DnsmasqLiveness> dnsmasq_alive;
    std::size_t rules{0};
    std::size_t domains{0};
};

// Runs the platform hook: `args[0]` is the hook path.
using DnsmasqExecFn = std::function<ExecCaptureResult(const std::vector<std::string>& args)>;
// Queues a task on a blocking worker; returns false if it was not accepted.
using DnsmasqPostFn = std::function<bool(std::function<void()>)>;

// Queries the config-hash.keen.pbr TXT record of the local dnsmasq.
using DnsmasqProbeFn = std::function<DnsTxtProbeResult()>;
// CLOCK_BOOTTIME in milliseconds.
using DnsmasqClockFn = std::function<std::int64_t()>;

struct DnsmasqTiming {
    // How long to wait for dnsmasq to serve the new config after `apply`.
    std::chrono::milliseconds confirm_timeout{15000};
    std::chrono::milliseconds confirm_interval{500};
    // Automatic repairs (apply because dnsmasq lost the config while the hash
    // did not change) are limited to `max_repair_attempts` per budget.  Attempt
    // N+1 starts no earlier than repair_delay * 2^(N-1) after attempt N started.
    // The budget refills after an explicit apply or after dnsmasq stayed in
    // sync for `stable_reset`.
    int max_repair_attempts{3};
    std::chrono::milliseconds repair_delay{std::chrono::minutes(5)};
    std::chrono::milliseconds stable_reset{std::chrono::hours(1)};
    // Defaults to std::this_thread::sleep_for.
    std::function<void(std::chrono::milliseconds)> sleep;
};

// Computes the hash of the dnsmasq config for dns.resolver_integration=dnsmasq
// entirely in memory (nothing is written to disk) and, when it changes, runs
// the OS-specific hook `apply`, which makes dnsmasq call `keen-pbr
// generate-resolver-config dnsmasq` and restarts it.  The generated config
// carries a TXT stamp (config-hash.keen.pbr) that is queried to confirm dnsmasq
// really loaded it.  `remove` undoes that when the integration is switched off
// inside the running daemon; while it is off dnsmasq is not touched at all.
// All work is blocking and is meant to run on a BlockingExecutor worker.
class DnsmasqManager {
public:
    explicit DnsmasqManager(std::string hook_path,
                            DnsmasqExecFn exec = {},
                            DnsmasqProbeFn probe = {},
                            DnsmasqClockFn clock = {},
                            DnsmasqTiming timing = {});

    // Blocking and serialized; never throws, failures end up in status().
    // `explicit_apply` (lifecycle operations the user asked for) installs the
    // config even when dnsmasq looks unchanged and refills the repair budget;
    // a changed hash does the same.  Otherwise a dnsmasq found out of sync
    // is an automatic repair that consumes the budget.  `reason` is only used
    // in logs of explicit applies.
    void sync(const Config& config,
              const DnsServerRegistry& registry,
              ListStreamer& streamer,
              bool explicit_apply = false,
              const std::string& reason = "config changed");

    // Same as sync(), but builds the registry and list streamer itself.
    void sync_from_config(const Config& config, const CacheManager& cache,
                          bool explicit_apply = false,
                          const std::string& reason = "config changed");

    // Coalescing entry point: remembers the latest config and runs
    // sync_from_config() through `post` unless a run is already queued or
    // active (that run then picks up the latest config).  `cache` must outlive
    // the posted work.
    void request_sync(Config config, const CacheManager& cache, const DnsmasqPostFn& post,
                      bool explicit_apply = false,
                      std::string reason = "config changed");

    // Coalesced with request_sync(): a cheap periodic probe that compares what
    // dnsmasq serves with the hash of the last full sync (no lists are
    // streamed) and repairs dnsmasq within the repair budget when it diverged.
    // A pending sync supersedes it.  Does nothing while the integration is off.
    void request_check(const DnsmasqPostFn& post);

    DnsmasqStatus status() const;

private:
    struct ProbeObservation {
        DnsTxtProbeResult raw;
        DnsmasqProbeState state{DnsmasqProbeState::NotChecked};
        std::optional<DnsmasqConfigStamp> stamp;
    };

    // How an apply was triggered (selects the log line and the budget use).
    struct ApplyContext {
        bool repair{false};
        // Explicit: why ("config changed", ...); repair: the problem sentence.
        std::string reason;
    };

    void post_worker(const DnsmasqPostFn& post);
    void run_check();
    void check_locked() REQUIRES(sync_mutex_);
    // Compares what dnsmasq serves with `hash`: marks it in sync, or decides
    // between a repair, a scheduled repair and doing nothing.  Used by the
    // periodic check and by full syncs whose hash did not change.
    void evaluate_locked(const std::string& hash, ProbeObservation observed) REQUIRES(sync_mutex_);
    ProbeObservation probe_once();
    DnsmasqLiveness query_liveness();
    void mark_in_sync(const DnsmasqConfigStamp& stamp, bool external) REQUIRES(sync_mutex_);
    // Handles a diverged dnsmasq: runs the next repair attempt, or schedules it
    // or pauses repairs, according to the budget.
    void repair_locked(const std::string& hash, const std::string& problem)
        REQUIRES(sync_mutex_);
    void reset_repair_budget() REQUIRES(sync_mutex_);
    void publish_state(DnsmasqSyncState state,
                       const std::optional<std::string>& error = std::nullopt,
                       std::optional<std::int64_t> next_repair_ts = std::nullopt)
        REQUIRES(sync_mutex_);
    // Runs the hook `apply` and waits for dnsmasq to serve `hash`.  Updates the
    // status.
    void apply_and_confirm(const std::string& hash, const ApplyContext& context)
        REQUIRES(sync_mutex_);
    std::string failure_message(const ProbeObservation& obs,
                                const std::string& expected_hash,
                                std::int64_t apply_started_ms) const;

    void sync_dnsmasq(const Config& config,
                      const DnsServerRegistry& registry,
                      ListStreamer& streamer,
                      bool explicit_apply,
                      const std::string& reason) REQUIRES(sync_mutex_);
    void sync_disabled() REQUIRES(sync_mutex_);
    void run_pending_requests();
    // Returns an empty string on success, otherwise the failure description.
    std::string run_hook(const std::vector<std::string>& args);
    void update_status(const std::function<void(DnsmasqStatus&)>& update);

    std::string hook_path_;
    DnsmasqExecFn exec_;
    DnsmasqProbeFn probe_;
    DnsmasqClockFn clock_;
    DnsmasqTiming timing_;
    std::atomic<bool> check_armed_{false};

    TracedMutex sync_mutex_;
    // Hash of the last full sync: what dnsmasq is expected to serve.
    std::string expected_hash_ GUARDED_BY(sync_mutex_);
    std::optional<DnsmasqConfigStamp> loaded_stamp_ GUARDED_BY(sync_mutex_);
    // CLOCK_BOOTTIME of the most recent `apply` we started (-1: none yet).
    std::int64_t apply_started_ms_ GUARDED_BY(sync_mutex_){-1};
    // Repair budget: attempts used, when the last one started (-1: none) and
    // whether the attempts ran out while the problem persisted.
    int repair_attempts_ GUARDED_BY(sync_mutex_){0};
    std::int64_t last_repair_started_ms_ GUARDED_BY(sync_mutex_){-1};
    bool repair_paused_ GUARDED_BY(sync_mutex_){false};
    std::string repair_reason_ GUARDED_BY(sync_mutex_);
    // Since when dnsmasq has been seen in sync without interruption (-1: not).
    std::int64_t in_sync_since_ms_ GUARDED_BY(sync_mutex_){-1};
    // Log de-duplication: the periodic check must stay silent while nothing
    // changes.
    int scheduled_logged_attempt_ GUARDED_BY(sync_mutex_){0};
    bool pause_logged_ GUARDED_BY(sync_mutex_){false};
    std::string not_answering_logged_ GUARDED_BY(sync_mutex_);
    ResolverIntegrationMode previous_mode_ GUARDED_BY(sync_mutex_){ResolverIntegrationMode::NONE};

    mutable TracedMutex status_mutex_;
    DnsmasqStatus status_ GUARDED_BY(status_mutex_);

    TracedMutex request_mutex_;
    std::optional<Config> pending_config_ GUARDED_BY(request_mutex_);
    const CacheManager* pending_cache_ GUARDED_BY(request_mutex_){nullptr};
    bool pending_check_ GUARDED_BY(request_mutex_){false};
    bool pending_explicit_apply_ GUARDED_BY(request_mutex_){false};
    std::string pending_reason_ GUARDED_BY(request_mutex_);
    bool worker_active_ GUARDED_BY(request_mutex_){false};
};

} // namespace keen_pbr3
