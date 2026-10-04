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
#include <system_error>
#include <thread>

#ifndef KEEN_PBR_DNSMASQ_PROBE_ADDRESS
#define KEEN_PBR_DNSMASQ_PROBE_ADDRESS "127.0.0.1:53"
#endif

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxHookOutputBytes = 2048;
constexpr const char* kProbeAddress = KEEN_PBR_DNSMASQ_PROBE_ADDRESS;
// Periodic checks in a row without an answer before dnsmasq is re-applied.
constexpr int kCheckQueryFailuresBeforeApply = 3;
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
                          bool bypass_backoff) {
    KPBR_LOCK_GUARD(sync_mutex_);
    const ResolverIntegrationMode mode = effective_resolver_integration(config);
    try {
        if (mode == ResolverIntegrationMode::DNSMASQ) {
            sync_dnsmasq(config, registry, streamer, bypass_backoff);
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
    first_sync_ = false;
    previous_mode_ = mode;
}

void DnsmasqManager::sync_from_config(const Config& config, const CacheManager& cache,
                                      bool bypass_backoff) {
    try {
        const DnsConfig dns = config.dns.value_or(DnsConfig{});
        // The registry resolves type=keenetic servers and is only needed (and
        // allowed to fail) when the integration is on.
        const DnsServerRegistry registry(
            effective_resolver_integration(config) == ResolverIntegrationMode::DNSMASQ
                ? dns
                : DnsConfig{});
        ListStreamer streamer(cache);
        sync(config, registry, streamer, bypass_backoff);
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
                                  bool bypass_backoff) {
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

    if (hash != expected_hash_ || bypass_backoff) {
        reset_backoff();
    }
    check_query_failures_ = 0;
    expected_hash_ = hash;
    check_armed_.store(true, std::memory_order_release);
    update_status([&](DnsmasqStatus& status) {
        status.mode = ResolverIntegrationMode::DNSMASQ;
        status.config_hash = hash;
        status.rules = stats.rules;
        status.domains = stats.domains;
    });

    // dnsmasq may already serve this exact config (keen-pbr restarted, or
    // dnsmasq started first): then there is nothing to install or restart.
    const ProbeObservation observed = probe_once();
    if (observed.stamp && observed.stamp->hash == hash) {
        mark_in_sync(*observed.stamp, /*external=*/false);
        return;
    }

    apply_and_confirm(hash, observed, "config changed or runtime (re)started");
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

void DnsmasqManager::mark_in_sync(const DnsmasqConfigStamp& stamp, bool external) {
    loaded_stamp_ = stamp;
    reset_backoff();
    update_status([&](DnsmasqStatus& status) {
        status.state = DnsmasqSyncState::Ok;
        status.last_error.clear();
        if (external) {
            status.last_external_reload_ts = stamp.unix_ts;
        }
    });
}

void DnsmasqManager::reset_backoff() {
    backoff_hash_.clear();
    backoff_until_ms_ = 0;
    backoff_delay_ = std::chrono::milliseconds{0};
}

void DnsmasqManager::record_apply_failure(const std::string& hash) {
    if (backoff_hash_ == hash && backoff_delay_.count() > 0) {
        backoff_delay_ = std::min(backoff_delay_ * 2, timing_.retry_backoff_max);
    } else {
        backoff_delay_ = std::min(timing_.retry_backoff_initial, timing_.retry_backoff_max);
    }
    backoff_hash_ = hash;
    backoff_until_ms_ = clock_() + backoff_delay_.count();
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

void DnsmasqManager::apply_and_confirm(const std::string& hash,
                                       const ProbeObservation& current,
                                       const std::string& reason) {
    auto& log = Logger::instance();

    if (backoff_hash_ == hash && clock_() < backoff_until_ms_) {
        const std::string message = failure_message(current, hash, apply_started_ms_);
        log.debug("dnsmasq apply for hash {} suppressed by retry backoff: {}", hash, message);
        update_status([&](DnsmasqStatus& status) {
            status.state = DnsmasqSyncState::Error;
            status.last_error = message;
        });
        return;
    }

    std::size_t rules = 0;
    std::size_t domains = 0;
    {
        const DnsmasqStatus snapshot = status();
        rules = snapshot.rules;
        domains = snapshot.domains;
    }
    apply_started_ms_ = clock_();
    update_status([&](DnsmasqStatus& status) {
        status.mode = ResolverIntegrationMode::DNSMASQ;
        status.state = DnsmasqSyncState::Applying;
    });
    log.info("Installing dnsmasq config ({} rule(s), {} domain(s), hash {}): {}",
             rules, domains, hash, reason);

    const std::string failure = run_hook({"apply"});
    if (!failure.empty()) {
        log.error("dnsmasq hook apply failed: {}", failure);
        record_apply_failure(hash);
        update_status([&](DnsmasqStatus& status) {
            status.state = DnsmasqSyncState::Error;
            status.last_error = failure;
        });
        return;
    }

    // dnsmasq runs the conf-script while it restarts; poll its stamp until it
    // serves this hash and was (re)started after we began applying.
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
            log.info("dnsmasq confirmed the config (hash {})", hash);
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
    log.error("dnsmasq config not confirmed: {}", message);
    record_apply_failure(hash);
    update_status([&](DnsmasqStatus& status) {
        status.state = DnsmasqSyncState::Error;
        status.last_error = message;
    });
}

void DnsmasqManager::check_locked() {
    auto& log = Logger::instance();
    if (expected_hash_.empty()) {
        return;  // not a dnsmasq-managed state (or no full sync yet)
    }
    const std::string hash = expected_hash_;
    const ProbeObservation observed = probe_once();

    if (observed.state == DnsmasqProbeState::QueryFailed &&
        ++check_query_failures_ < kCheckQueryFailuresBeforeApply) {
        log.debug("dnsmasq config check got no answer ({} in a row): {}",
                  check_query_failures_, observed.raw.error);
        return;
    }
    if (observed.state != DnsmasqProbeState::QueryFailed) {
        check_query_failures_ = 0;
    }

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

    const bool restarted_externally =
        observed.stamp && observed.stamp->boottime_ms > apply_started_ms_;
    if (restarted_externally) {
        log.warn("dnsmasq was restarted outside keen-pbr with a different config "
                 "(hash {}, expected {}); re-applying", observed.stamp->hash, hash);
    } else if (observed.state == DnsmasqProbeState::Missing) {
        log.warn("dnsmasq no longer serves the keen-pbr config (TXT missing); re-applying");
    } else {
        log.warn("dnsmasq config check failed: {}; re-applying",
                 failure_message(observed, hash, apply_started_ms_));
    }
    apply_and_confirm(hash, observed, "periodic check found dnsmasq out of sync");
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
    // remove is idempotent: always run it on the first sync (a previous run
    // may have left the hook installed), afterwards only on dnsmasq -> none.
    const bool needs_remove = first_sync_ || previous_mode_ == ResolverIntegrationMode::DNSMASQ;

    std::string failure;
    if (needs_remove) {
        if (!hook_path_.empty()) {
            failure = run_hook({"remove"});
            if (!failure.empty()) {
                Logger::instance().error("dnsmasq hook remove failed: {}", failure);
            }
        }
    }
    expected_hash_.clear();
    loaded_stamp_.reset();
    apply_started_ms_ = -1;
    reset_backoff();
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
                                  bool bypass_backoff) {
    {
        KPBR_LOCK_GUARD(request_mutex_);
        pending_config_ = std::move(config);
        pending_cache_ = &cache;
        pending_bypass_backoff_ = pending_bypass_backoff_ || bypass_backoff;
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
        return;
    }
    {
        KPBR_LOCK_GUARD(request_mutex_);
        if (pending_config_.has_value()) {
            return;  // a queued sync supersedes the check
        }
        pending_check_ = true;
        if (worker_active_) {
            return;
        }
        worker_active_ = true;
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
        bool bypass_backoff = false;
        {
            KPBR_LOCK_GUARD(request_mutex_);
            if (pending_config_.has_value()) {
                config = std::move(*pending_config_);
                pending_config_.reset();
                cache = pending_cache_;
                bypass_backoff = pending_bypass_backoff_;
                pending_bypass_backoff_ = false;
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
            sync_from_config(config, *cache, bypass_backoff);
        }
    }
}

} // namespace keen_pbr3
