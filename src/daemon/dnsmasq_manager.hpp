#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../dns/dns_router.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/safe_exec.hpp"
#include "../util/traced_mutex.hpp"

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

// Snapshot of the generated dnsmasq config state, exposed through health.
struct DnsmasqStatus {
    ResolverIntegrationMode mode{ResolverIntegrationMode::NONE};
    DnsmasqSyncState state{DnsmasqSyncState::Disabled};
    // MD5 of the config last installed successfully (empty if none).
    std::string config_hash;
    std::optional<std::int64_t> last_apply_ts;
    std::string last_error;
    std::size_t rules{0};
    std::size_t domains{0};
};

// Runs the platform hook: `args[0]` is the hook path.
using DnsmasqExecFn = std::function<ExecCaptureResult(const std::vector<std::string>& args)>;
// Queues a task on a blocking worker; returns false if it was not accepted.
using DnsmasqPostFn = std::function<bool(std::function<void()>)>;

// Computes the hash of the dnsmasq config for dns.resolver_integration=dnsmasq
// entirely in memory (nothing is written to disk) and, when it changes, runs
// the OS-specific hook `apply`, which makes dnsmasq call `keen-pbr
// dnsmasq-config` and restarts it.  `remove` undoes that when the integration
// is switched off.  All work is blocking and is
// meant to run on a BlockingExecutor worker.
class DnsmasqManager {
public:
    explicit DnsmasqManager(std::string hook_path, DnsmasqExecFn exec = {});

    // Blocking and serialized; never throws, failures end up in status().
    void sync(const Config& config,
              const DnsServerRegistry& registry,
              ListStreamer& streamer);

    // Same as sync(), but builds the registry and list streamer itself.
    void sync_from_config(const Config& config, const CacheManager& cache);

    // Coalescing entry point: remembers the latest config and runs
    // sync_from_config() through `post` unless a run is already queued or
    // active (that run then picks up the latest config).  `cache` must outlive
    // the posted work.
    void request_sync(Config config, const CacheManager& cache, const DnsmasqPostFn& post);

    DnsmasqStatus status() const;

private:
    void sync_dnsmasq(const Config& config,
                      const DnsServerRegistry& registry,
                      ListStreamer& streamer) REQUIRES(sync_mutex_);
    void sync_disabled() REQUIRES(sync_mutex_);
    void run_pending_requests();
    // Returns an empty string on success, otherwise the failure description.
    std::string run_hook(const std::vector<std::string>& args);
    void update_status(const std::function<void(DnsmasqStatus&)>& update);

    std::string hook_path_;
    DnsmasqExecFn exec_;

    TracedMutex sync_mutex_;
    std::string last_applied_hash_ GUARDED_BY(sync_mutex_);
    bool first_sync_ GUARDED_BY(sync_mutex_){true};
    ResolverIntegrationMode previous_mode_ GUARDED_BY(sync_mutex_){ResolverIntegrationMode::NONE};

    mutable TracedMutex status_mutex_;
    DnsmasqStatus status_ GUARDED_BY(status_mutex_);

    TracedMutex request_mutex_;
    std::optional<Config> pending_config_ GUARDED_BY(request_mutex_);
    const CacheManager* pending_cache_ GUARDED_BY(request_mutex_){nullptr};
    bool worker_active_ GUARDED_BY(request_mutex_){false};
};

} // namespace keen_pbr3
