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
    Error,
};

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
    // After a failed apply for a hash, `apply` is not repeated for that hash
    // until the backoff expires; it doubles on every failure up to the maximum.
    std::chrono::milliseconds retry_backoff_initial{std::chrono::minutes(5)};
    std::chrono::milliseconds retry_backoff_max{std::chrono::minutes(30)};
    // Defaults to std::this_thread::sleep_for.
    std::function<void(std::chrono::milliseconds)> sleep;
};

// Computes the hash of the dnsmasq config for dns.resolver_integration=dnsmasq
// entirely in memory (nothing is written to disk) and, when it changes, runs
// the OS-specific hook `apply`, which makes dnsmasq call `keen-pbr
// dnsmasq-config` and restarts it.  The generated config carries a TXT stamp
// (config-hash.keen.pbr) that is queried to confirm dnsmasq really loaded it.  `remove` undoes that when the integration
// is switched off.  All work is blocking and is
// meant to run on a BlockingExecutor worker.
class DnsmasqManager {
public:
    explicit DnsmasqManager(std::string hook_path,
                            DnsmasqExecFn exec = {},
                            DnsmasqProbeFn probe = {},
                            DnsmasqClockFn clock = {},
                            DnsmasqTiming timing = {});

    // Blocking and serialized; never throws, failures end up in status().
    // `bypass_backoff` (lifecycle operations the user asked for) re-applies
    // even while a previous failure for the same hash is backing off.
    void sync(const Config& config,
              const DnsServerRegistry& registry,
              ListStreamer& streamer,
              bool bypass_backoff = false);

    // Same as sync(), but builds the registry and list streamer itself.
    void sync_from_config(const Config& config, const CacheManager& cache,
                          bool bypass_backoff = false);

    // Coalescing entry point: remembers the latest config and runs
    // sync_from_config() through `post` unless a run is already queued or
    // active (that run then picks up the latest config).  `cache` must outlive
    // the posted work.
    void request_sync(Config config, const CacheManager& cache, const DnsmasqPostFn& post,
                      bool bypass_backoff = false);

    // Coalesced with request_sync(): a cheap periodic probe that compares what
    // dnsmasq serves with the hash of the last full sync (no lists are
    // streamed) and re-applies when it diverged.  A pending sync supersedes it.
    void request_check(const DnsmasqPostFn& post);

    DnsmasqStatus status() const;

private:
    struct ProbeObservation {
        DnsTxtProbeResult raw;
        DnsmasqProbeState state{DnsmasqProbeState::NotChecked};
        std::optional<DnsmasqConfigStamp> stamp;
    };

    void post_worker(const DnsmasqPostFn& post);
    void run_check();
    void check_locked() REQUIRES(sync_mutex_);
    ProbeObservation probe_once();
    void mark_in_sync(const DnsmasqConfigStamp& stamp, bool external) REQUIRES(sync_mutex_);
    // Runs the hook `apply` and waits for dnsmasq to serve `hash` (subject to
    // the retry backoff).  Updates the status.
    void apply_and_confirm(const std::string& hash,
                           const ProbeObservation& current,
                           const std::string& reason) REQUIRES(sync_mutex_);
    void record_apply_failure(const std::string& hash) REQUIRES(sync_mutex_);
    void reset_backoff() REQUIRES(sync_mutex_);
    std::string failure_message(const ProbeObservation& obs,
                                const std::string& expected_hash,
                                std::int64_t apply_started_ms) const;

    void sync_dnsmasq(const Config& config,
                      const DnsServerRegistry& registry,
                      ListStreamer& streamer,
                      bool bypass_backoff) REQUIRES(sync_mutex_);
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
    std::string backoff_hash_ GUARDED_BY(sync_mutex_);
    std::int64_t backoff_until_ms_ GUARDED_BY(sync_mutex_){0};
    std::chrono::milliseconds backoff_delay_ GUARDED_BY(sync_mutex_){0};
    // Consecutive periodic checks dnsmasq did not answer; a single lost UDP
    // query must not restart it.
    int check_query_failures_ GUARDED_BY(sync_mutex_){0};
    bool first_sync_ GUARDED_BY(sync_mutex_){true};
    ResolverIntegrationMode previous_mode_ GUARDED_BY(sync_mutex_){ResolverIntegrationMode::NONE};

    mutable TracedMutex status_mutex_;
    DnsmasqStatus status_ GUARDED_BY(status_mutex_);

    TracedMutex request_mutex_;
    std::optional<Config> pending_config_ GUARDED_BY(request_mutex_);
    const CacheManager* pending_cache_ GUARDED_BY(request_mutex_){nullptr};
    bool pending_check_ GUARDED_BY(request_mutex_){false};
    bool pending_bypass_backoff_ GUARDED_BY(request_mutex_){false};
    bool worker_active_ GUARDED_BY(request_mutex_){false};
};

} // namespace keen_pbr3
