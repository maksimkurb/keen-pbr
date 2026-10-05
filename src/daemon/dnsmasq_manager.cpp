#include "dnsmasq_manager.hpp"

#include "../dns/dns_server.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../log/logger.hpp"
#include "../util/time_utils.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <ostream>
#include <streambuf>
#include <thread>

#ifndef KEEN_PBR_DNSMASQ_PROBE_ADDRESS
#define KEEN_PBR_DNSMASQ_PROBE_ADDRESS "127.0.0.1:53"
#endif

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxHookOutputBytes = 2048;
constexpr const char* kProbeAddress = KEEN_PBR_DNSMASQ_PROBE_ADDRESS;
constexpr std::chrono::milliseconds kProbeTimeout{1000};

// Discards everything written to it: the generator hashes the bytes itself,
// so nothing is ever stored (lists may hold 100k+ domains).
class NullStreamBuf : public std::streambuf {
protected:
    int_type overflow(int_type c) override { return traits_type::not_eof(c); }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};

std::string trim_copy(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

ExecCaptureResult default_exec(const std::vector<std::string>& args) {
    return safe_exec_capture(args, /*suppress_stderr=*/true, kMaxHookOutputBytes,
                             /*merge_stderr=*/true);
}

DnsTxtProbeResult default_probe() {
    try {
        const ParsedDnsAddress address = parse_dns_address_str(kProbeAddress);
        return probe_dns_txt(address.ip, address.port, kDnsmasqStampDomain, kProbeTimeout);
    } catch (const std::exception& error) {
        DnsTxtProbeResult result;
        result.status = DnsTxtProbeStatus::QueryFailed;
        result.error = error.what();
        return result;
    }
}

std::string format_unix_ts(std::int64_t ts) {
    const auto seconds = static_cast<std::time_t>(ts);
    std::tm tm{};
    if (gmtime_r(&seconds, &tm) == nullptr) {
        return std::to_string(ts);
    }
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &tm) == 0) {
        return std::to_string(ts);
    }
    return buffer;
}

std::string format_local_ts(std::int64_t ts) {
    const auto seconds = static_cast<std::time_t>(ts);
    std::tm tm{};
    char buffer[32];
    if (localtime_r(&seconds, &tm) == nullptr ||
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
        return std::to_string(ts);
    }
    return buffer;
}

} // namespace

DnsmasqManager::DnsmasqManager(std::string hook_path,
                               DnsmasqExecFn exec,
                               DnsmasqProbeFn probe,
                               DnsmasqClockFn clock,
                               DnsmasqTiming timing)
    : hook_path_(std::move(hook_path)),
      exec_(exec ? std::move(exec) : DnsmasqExecFn(default_exec)),
      probe_(probe ? std::move(probe) : DnsmasqProbeFn(default_probe)),
      clock_(clock ? std::move(clock) : DnsmasqClockFn(boottime_now_ms)),
      timing_(std::move(timing)) {
    if (!timing_.sleep) {
        timing_.sleep = [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); };
    }
}

DnsmasqStatus DnsmasqManager::status() const {
    KPBR_LOCK_GUARD(status_mutex_);
    return status_;
}

void DnsmasqManager::update_status(const std::function<void(DnsmasqStatus&)>& update) {
    KPBR_LOCK_GUARD(status_mutex_);
    update(status_);
}

std::string DnsmasqManager::run_hook(const std::vector<std::string>& args) {
    std::vector<std::string> command{hook_path_};
    command.insert(command.end(), args.begin(), args.end());
    const ExecCaptureResult result = exec_(command);
    if (result.exit_code == 0 && !result.timed_out) {
        return {};
    }
    std::string message = result.timed_out
        ? std::string("hook timed out")
        : "hook exited " + std::to_string(result.exit_code);
    const std::string output = trim_copy(result.stdout_output);
    if (!output.empty()) {
        message += ": " + output;
    }
    return message;
}

void DnsmasqManager::sync(const Config& config,
                          const DnsServerRegistry& registry,
                          ListStreamer& streamer,
                          bool explicit_apply,
                          const std::string& reason) {
    KPBR_LOCK_GUARD(sync_mutex_);
    const ResolverIntegrationMode mode = effective_resolver_integration(config);
    try {
        if (dnsmasq_integration_enabled(config)) {
            sync_dnsmasq(config, registry, streamer, explicit_apply, reason);
        } else {
            sync_disabled();
        }
    } catch (const std::exception& error) {
        Logger::instance().error("dnsmasq config sync failed: {}", error.what());
        update_status([&](DnsmasqStatus& status) {
            status.mode = mode;
            status.state = DnsmasqSyncState::Error;
            status.last_error = error.what();
        });
    }
    previous_mode_ = mode;
}

void DnsmasqManager::sync_from_config(const Config& config, const CacheManager& cache,
                                      bool explicit_apply, const std::string& reason) {
    try {
        const DnsConfig dns = config.dns.value_or(DnsConfig{});
        // The registry resolves type=keenetic servers and is only needed (and
        // allowed to fail) when the integration is on.
        const DnsServerRegistry registry(dnsmasq_integration_enabled(config) ? dns : DnsConfig{});
        ListStreamer streamer(cache);
        sync(config, registry, streamer, explicit_apply, reason);
    } catch (const std::exception& error) {
        Logger::instance().error("dnsmasq config sync failed: {}", error.what());
        update_status([&](DnsmasqStatus& status) {
            status.mode = effective_resolver_integration(config);
            status.state = DnsmasqSyncState::Error;
            status.last_error = error.what();
        });
    }
}

void DnsmasqManager::sync_dnsmasq(const Config& config,
                                  const DnsServerRegistry& registry,
                                  ListStreamer& streamer,
                                  bool explicit_apply,
                                  const std::string& reason) {
    auto& log = Logger::instance();
    if (hook_path_.empty()) {
        expected_hash_.clear();
        check_armed_.store(false, std::memory_order_release);
        update_status([](DnsmasqStatus& status) {
            status.mode = ResolverIntegrationMode::DNSMASQ;
            status.state = DnsmasqSyncState::Error;
            status.last_error = "no dnsmasq hook for this platform";
        });
        log.warn("dns.resolver_integration is dnsmasq, but this build has no dnsmasq hook");
        return;
    }

    std::string hash;
    DnsmasqGenStats stats;
    {
        NullStreamBuf null_buf;
        std::ostream null_out(&null_buf);
        const DnsConfig dns = config.dns.value_or(DnsConfig{});
        const auto lists = config.lists.value_or(std::map<std::string, ListConfig>{});
        DnsmasqGenerator generator(registry, streamer, dns, lists);
        hash = generator.generate(null_out, &stats);
    }

    // A changed hash or a lifecycle sync is an explicit apply: it does not use
    // up the repair budget and refills it.
    const bool explicit_now = explicit_apply || hash != expected_hash_;
    if (explicit_now) {
        reset_repair_budget();
    }
    expected_hash_ = hash;
    check_armed_.store(true, std::memory_order_release);
    update_status([&](DnsmasqStatus& status) {
        status.mode = ResolverIntegrationMode::DNSMASQ;
        status.config_hash = hash;
        status.rules = stats.rules;
        status.domains = stats.domains;
    });

    if (!explicit_now) {
        // Same config as before (e.g. lists autoupdate): only repair dnsmasq
        // if it lost the config, within the repair budget.
        evaluate_locked(hash, probe_once());
        return;
    }

    // dnsmasq may already serve this exact config (keen-pbr restarted, or
    // dnsmasq started first): then there is nothing to install or restart.
    const ProbeObservation observed = probe_once();
    if (observed.stamp && observed.stamp->hash == hash) {
        log.info("dnsmasq already serves keen-pbr config (hash {}); not restarting", hash);
        mark_in_sync(*observed.stamp, /*external=*/false);
        return;
    }

    apply_and_confirm(hash, ApplyContext{false, reason});
}

DnsmasqManager::ProbeObservation DnsmasqManager::probe_once() {
    ProbeObservation observation;
    observation.raw = probe_();
    switch (observation.raw.status) {
    case DnsTxtProbeStatus::Ok:
        observation.stamp = parse_dnsmasq_config_stamp(observation.raw.txt);
        observation.state = observation.stamp ? DnsmasqProbeState::Ok : DnsmasqProbeState::Invalid;
        break;
    case DnsTxtProbeStatus::Missing:
        observation.state = DnsmasqProbeState::Missing;
        break;
    case DnsTxtProbeStatus::IdMismatch:
        // Should not occur; handled inside probe_dns_txt retry loop.
        // If it escapes, treat as a transient error.
        observation.state = DnsmasqProbeState::QueryFailed;
        break;
    case DnsTxtProbeStatus::QueryFailed:
        observation.state = DnsmasqProbeState::QueryFailed;
        break;
    }
    const std::int64_t now = unix_timestamp_now_seconds();
    update_status([&](DnsmasqStatus& status) {
        status.probe_state = observation.state;
        status.last_check_ts = now;
        if (observation.stamp) {
            status.loaded_hash = observation.stamp->hash;
            status.loaded_boottime_ms = observation.stamp->boottime_ms;
            status.loaded_ts = observation.stamp->unix_ts;
        } else if (observation.state != DnsmasqProbeState::QueryFailed) {
            // dnsmasq answered and reports no keen-pbr config; keep the last
            // known values only when it could not be asked.
            status.loaded_hash.clear();
            status.loaded_boottime_ms.reset();
            status.loaded_ts.reset();
        }
    });
    return observation;
}

DnsmasqLiveness DnsmasqManager::query_liveness() {
    const ExecCaptureResult result = exec_({hook_path_, "alive"});
    DnsmasqLiveness liveness = DnsmasqLiveness::Unknown;
    if (!result.timed_out) {
        if (result.exit_code == 0) liveness = DnsmasqLiveness::Alive;
        else if (result.exit_code == 1) liveness = DnsmasqLiveness::Dead;
    }
    update_status([&](DnsmasqStatus& status) { status.dnsmasq_alive = liveness; });
    return liveness;
}

void DnsmasqManager::publish_state(DnsmasqSyncState state,
                                   const std::optional<std::string>& error,
                                   std::optional<std::int64_t> next_repair_ts) {
    const int attempt = repair_attempts_;
    const int max_attempts = timing_.max_repair_attempts;
    const bool paused = repair_paused_;
    const std::string reason = repair_reason_;
    update_status([&](DnsmasqStatus& status) {
        status.mode = ResolverIntegrationMode::DNSMASQ;
        status.state = state;
        if (error) status.last_error = *error;
        status.repair_attempt = attempt;
        status.repair_max_attempts = max_attempts;
        status.repair_paused = paused;
        status.repair_reason = reason;
        status.next_repair_ts = next_repair_ts;
    });
}

void DnsmasqManager::reset_repair_budget() {
    repair_attempts_ = 0;
    last_repair_started_ms_ = -1;
    repair_paused_ = false;
    repair_reason_.clear();
    scheduled_logged_attempt_ = 0;
    pause_logged_ = false;
}

void DnsmasqManager::mark_in_sync(const DnsmasqConfigStamp& stamp, bool external) {
    loaded_stamp_ = stamp;
    not_answering_logged_.clear();
    scheduled_logged_attempt_ = 0;

    // Stable for long enough since the last repair: refill the budget.
    const std::int64_t now = clock_();
    if (in_sync_since_ms_ < 0) {
        in_sync_since_ms_ = now;
    }
    if ((repair_attempts_ > 0 || repair_paused_) &&
        now - in_sync_since_ms_ >= timing_.stable_reset.count()) {
        reset_repair_budget();
    }

    publish_state(DnsmasqSyncState::Ok, std::string{});
    if (external) {
        update_status([&](DnsmasqStatus& status) {
            status.last_external_reload_ts = stamp.unix_ts;
        });
    }
}

std::string DnsmasqManager::failure_message(const ProbeObservation& obs,
                                            const std::string& expected_hash,
                                            std::int64_t apply_started_ms) const {
    switch (obs.state) {
    case DnsmasqProbeState::Missing:
        return "dnsmasq is running without the keen-pbr config (TXT " +
               std::string(kDnsmasqStampDomain) +
               " missing); the dnsmasq hook did not install its drop-in where dnsmasq reads it";
    case DnsmasqProbeState::QueryFailed:
        return std::string("dnsmasq did not answer the config check on ") + kProbeAddress +
               ": " + obs.raw.error;
    case DnsmasqProbeState::Invalid:
        return "invalid " + std::string(kDnsmasqStampDomain) + " TXT record: " + obs.raw.txt;
    case DnsmasqProbeState::Ok:
        break;
    case DnsmasqProbeState::NotChecked:
        return "dnsmasq config check did not run";
    }
    if (obs.stamp->boottime_ms < apply_started_ms) {
        return "dnsmasq was not restarted after apply (it last loaded a config at " +
               format_unix_ts(obs.stamp->unix_ts) + ")";
    }
    return "dnsmasq restarted but loaded a different config (hash " + obs.stamp->hash +
           ", expected " + expected_hash + ")";
}

void DnsmasqManager::apply_and_confirm(const std::string& hash, const ApplyContext& context) {
    auto& log = Logger::instance();

    apply_started_ms_ = clock_();
    in_sync_since_ms_ = -1;
    publish_state(DnsmasqSyncState::Applying, std::string{});
    if (context.repair) {
        log.info("Restarting dnsmasq (repair attempt {}/{}) because {}",
                 repair_attempts_, timing_.max_repair_attempts, context.reason);
    } else {
        log.info("Restarting dnsmasq to install keen-pbr config: {} (hash {})",
                 context.reason, hash);
    }

    const std::string failure = run_hook({"apply"});
    if (!failure.empty()) {
        log.error("dnsmasq hook apply failed: {}", failure);
        publish_state(DnsmasqSyncState::Error, failure);
        return;
    }

    // dnsmasq runs the conf-script while it restarts; poll its stamp until it
    // serves this hash and was (re)started after we began applying.  An
    // unanswered query keeps polling: dnsmasq is restarting.
    const auto started = std::chrono::steady_clock::now();
    std::chrono::milliseconds waited{0};
    ProbeObservation last;
    while (true) {
        last = probe_once();
        if (last.stamp && last.stamp->hash == hash &&
            last.stamp->boottime_ms >= apply_started_ms_) {
            const std::int64_t now = unix_timestamp_now_seconds();
            mark_in_sync(*last.stamp, /*external=*/false);
            update_status([&](DnsmasqStatus& status) { status.last_apply_ts = now; });
            log.info("dnsmasq confirmed keen-pbr config (hash {})", hash);
            return;
        }
        const auto real_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        if (std::max(waited, real_elapsed) >= timing_.confirm_timeout) {
            break;
        }
        timing_.sleep(timing_.confirm_interval);
        waited += timing_.confirm_interval;
    }

    const std::string message = failure_message(last, hash, apply_started_ms_);
    log.error("dnsmasq did not confirm keen-pbr config: {}", message);
    publish_state(DnsmasqSyncState::Error, message);
}

void DnsmasqManager::repair_locked(const std::string& hash, const std::string& problem) {
    auto& log = Logger::instance();
    in_sync_since_ms_ = -1;
    repair_reason_ = problem;
    not_answering_logged_.clear();

    if (repair_paused_ || repair_attempts_ >= timing_.max_repair_attempts) {
        repair_paused_ = true;
        const std::string message =
            "dnsmasq lost the keen-pbr config again after " +
            std::to_string(timing_.max_repair_attempts) +
            " repair attempts; automatic repair is paused until the next config apply or "
            "runtime restart (last problem: " + problem + ")";
        if (!pause_logged_) {
            pause_logged_ = true;
            log.error("{}", message);
        }
        publish_state(DnsmasqSyncState::Error, message);
        return;
    }

    const std::int64_t now = clock_();
    if (repair_attempts_ > 0) {
        const std::int64_t due = last_repair_started_ms_ +
            timing_.repair_delay.count() * (std::int64_t{1} << (repair_attempts_ - 1));
        if (now < due) {
            const std::int64_t due_ts = unix_timestamp_now_seconds() + (due - now + 999) / 1000;
            if (scheduled_logged_attempt_ != repair_attempts_ + 1) {
                scheduled_logged_attempt_ = repair_attempts_ + 1;
                log.warn("dnsmasq out of sync: {}; repair attempt {}/{} scheduled at {}",
                         problem, repair_attempts_ + 1, timing_.max_repair_attempts,
                         format_local_ts(due_ts));
            }
            publish_state(DnsmasqSyncState::Reconciling, std::nullopt, due_ts);
            return;
        }
    }

    ++repair_attempts_;
    last_repair_started_ms_ = now;
    scheduled_logged_attempt_ = 0;
    apply_and_confirm(hash, ApplyContext{true, problem});
}

void DnsmasqManager::evaluate_locked(const std::string& hash, ProbeObservation observed) {
    auto& log = Logger::instance();

    std::string problem;
    if (observed.state == DnsmasqProbeState::QueryFailed) {
        // One lost UDP datagram is no signal: ask again right away.
        observed = probe_once();
    }
    if (observed.state == DnsmasqProbeState::QueryFailed) {
        // No answer is not a reason to restart a running dnsmasq (it may
        // listen elsewhere); only a dead one is repaired.
        const DnsmasqLiveness liveness = query_liveness();
        if (liveness == DnsmasqLiveness::Dead) {
            repair_locked(hash, "dnsmasq is not running");
            return;
        }
        const std::string message = liveness == DnsmasqLiveness::Alive
            ? "dnsmasq is running but does not answer the config check on " +
                  std::string(kProbeAddress) + ": " + observed.raw.error +
                  "; not restarting it"
            : "could not tell whether dnsmasq is running and it does not answer the "
              "config check on " + std::string(kProbeAddress) + ": " + observed.raw.error +
              "; not restarting it";
        if (not_answering_logged_ != message) {
            not_answering_logged_ = message;
            log.warn("{}", message);
        }
        in_sync_since_ms_ = -1;
        publish_state(DnsmasqSyncState::Error, message);
        return;
    }
    not_answering_logged_.clear();

    if (observed.stamp && observed.stamp->hash == hash) {
        const DnsmasqConfigStamp& stamp = *observed.stamp;
        const bool newer = !loaded_stamp_ || stamp.boottime_ms > loaded_stamp_->boottime_ms;
        if (newer) {
            log.info("dnsmasq was restarted outside keen-pbr and reloaded the config "
                     "(hash {})", hash);
        }
        mark_in_sync(stamp, newer);
        return;
    }

    if (observed.state == DnsmasqProbeState::Invalid) {
        problem = "dnsmasq answered with an invalid " + std::string(kDnsmasqStampDomain) +
                  " record";
    } else if (observed.stamp && observed.stamp->boottime_ms > apply_started_ms_) {
        problem = "dnsmasq was restarted outside keen-pbr and lost the keen-pbr config";
    } else if (observed.stamp) {
        problem = "dnsmasq serves a different config (hash " + observed.stamp->hash +
                  ", expected " + hash + ")";
    } else {
        problem = "the keen-pbr config is missing from dnsmasq (TXT " +
                  std::string(kDnsmasqStampDomain) + " not found)";
    }
    repair_locked(hash, problem);
}

void DnsmasqManager::check_locked() {
    if (expected_hash_.empty()) {
        return;  // not a dnsmasq-managed state (or no full sync yet)
    }
    evaluate_locked(expected_hash_, probe_once());
}

void DnsmasqManager::run_check() {
    KPBR_LOCK_GUARD(sync_mutex_);
    try {
        check_locked();
    } catch (const std::exception& error) {
        Logger::instance().error("dnsmasq config check failed: {}", error.what());
    }
}

void DnsmasqManager::sync_disabled() {
    // The integration is off: dnsmasq is not touched, not even to clean up
    // what an earlier run may have left (the conf-script prints only a comment
    // while the integration is none).  Only a switch-off inside this daemon
    // removes the config.
    std::string failure;
    if (previous_mode_ == ResolverIntegrationMode::DNSMASQ && !hook_path_.empty()) {
        Logger::instance().info("Removing keen-pbr dnsmasq config: integration disabled");
        failure = run_hook({"remove"});
        if (!failure.empty()) {
            Logger::instance().error("dnsmasq hook remove failed: {}", failure);
        }
    }
    expected_hash_.clear();
    loaded_stamp_.reset();
    apply_started_ms_ = -1;
    in_sync_since_ms_ = -1;
    reset_repair_budget();
    not_answering_logged_.clear();
    check_armed_.store(false, std::memory_order_release);

    update_status([&](DnsmasqStatus& status) {
        status = DnsmasqStatus{};
        if (!failure.empty()) {
            status.state = DnsmasqSyncState::Error;
            status.last_error = failure;
        }
    });
}

void DnsmasqManager::request_sync(Config config,
                                  const CacheManager& cache,
                                  const DnsmasqPostFn& post,
                                  bool explicit_apply,
                                  std::string reason) {
    {
        KPBR_LOCK_GUARD(request_mutex_);
        pending_config_ = std::move(config);
        pending_cache_ = &cache;
        // An explicit request keeps its reason when a plain one coalesces into it.
        if (explicit_apply || !pending_explicit_apply_) {
            pending_reason_ = std::move(reason);
        }
        pending_explicit_apply_ = pending_explicit_apply_ || explicit_apply;
        pending_check_ = false;  // the sync probes dnsmasq as well
        if (worker_active_) {
            return;  // the active run picks up the latest config
        }
        worker_active_ = true;
    }
    post_worker(post);
}

void DnsmasqManager::request_check(const DnsmasqPostFn& post) {
    if (!check_armed_.load(std::memory_order_acquire)) {
        return;  // integration off (or nothing synced yet): never touch dnsmasq
    }
    {
        KPBR_LOCK_GUARD(request_mutex_);
        if (pending_config_.has_value()) {
            // A queued sync supersedes the check. If the worker is not active,
            // it means post_worker failed earlier, so re-arm it to retry the sync.
            if (!worker_active_) {
                worker_active_ = true;
            } else {
                return;  // sync is already queued and will run
            }
        } else {
            pending_check_ = true;
            if (worker_active_) {
                return;
            }
            worker_active_ = true;
        }
    }
    post_worker(post);
}

void DnsmasqManager::post_worker(const DnsmasqPostFn& post) {
    if (!post([this] { run_pending_requests(); })) {
        Logger::instance().warn("dnsmasq config sync not scheduled: executor unavailable");
        KPBR_LOCK_GUARD(request_mutex_);
        worker_active_ = false;
        pending_check_ = false;
    }
}

void DnsmasqManager::run_pending_requests() {
    while (true) {
        Config config;
        const CacheManager* cache = nullptr;
        bool check_only = false;
        bool explicit_apply = false;
        std::string reason;
        {
            KPBR_LOCK_GUARD(request_mutex_);
            if (pending_config_.has_value()) {
                config = std::move(*pending_config_);
                pending_config_.reset();
                cache = pending_cache_;
                explicit_apply = pending_explicit_apply_;
                reason = std::move(pending_reason_);
                pending_explicit_apply_ = false;
                pending_reason_.clear();
            } else if (pending_check_) {
                pending_check_ = false;
                check_only = true;
            } else {
                worker_active_ = false;
                return;
            }
        }
        if (check_only) {
            run_check();
        } else {
            sync_from_config(config, *cache, explicit_apply, reason);
        }
    }
}

} // namespace keen_pbr3
