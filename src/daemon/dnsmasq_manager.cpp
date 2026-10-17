#include "dnsmasq_manager.hpp"

#include "../dns/dnsmasq_gen.hpp"
#include "../log/logger.hpp"
#include "../util/time_utils.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <ostream>
#include <streambuf>
#include <thread>

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxHookOutputBytes = 2048;
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
                               DnsmasqManagerDeps deps,
                               DnsmasqTiming timing)
    : hook_path_(std::move(hook_path)),
      timing_(std::move(timing)) {
    exec_ = deps.exec ? std::move(deps.exec) : DnsmasqExecFn(default_exec);
    probe_ = deps.probe ? std::move(deps.probe) : DnsmasqProbeFn([](const DnsmasqEndpoint& ep) {
        return probe_dns_txt(ep.address, ep.port, kDnsmasqStampDomain, kProbeTimeout);
    });
    clock_ = deps.clock ? std::move(deps.clock) : DnsmasqClockFn(boottime_now_ms);
    if (deps.detect) {
        detect_ = std::move(deps.detect);
    } else {
        const std::string path = hook_path_;
        detect_ = [path](const DnsmasqDetectOptions& o) { return DnsmasqDetector(path).detect(o); };
    }
    capability_fn_ = deps.capability ? std::move(deps.capability)
        : DnsmasqCapabilityFn([](const std::string& p) { return probe_dnsmasq_capabilities(p); });
    preflight_fn_ = deps.preflight ? std::move(deps.preflight)
        : DnsmasqPreflightFn([](const DnsmasqInstance& i) { return run_dnsmasq_preflight(i); });
    balancer_groups_ = std::move(deps.balancer_groups);
    if (!timing_.sleep) {
        timing_.sleep = [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); };
    }
}

RoutingDnsResolver DnsmasqManager::routing_resolver(const Config& config) const {
    RoutingDnsResolver r;
    if (!dnsmasq_integration_enabled(config)) return r;  // System
    r.source = RoutingDnsSource::Dnsmasq;
    const DnsmasqStatus s = status();
    if (!s.endpoint) {
        r.unavailable =
            std::string("dnsmasq integration is enabled, but no usable dnsmasq DNS endpoint is known") +
            (s.error_code != DnsmasqErrorCode::None
                 ? std::string(" (") + dnsmasq_error_code_name(s.error_code) + ": " + s.last_error + ")"
                 : std::string());
        return r;
    }
    r.address = s.endpoint->address;
    r.port = s.endpoint->port;
    r.instance = s.selector;
    if (s.state != DnsmasqSyncState::Ok) {
        r.warning = "dnsmasq does not currently confirm the keen-pbr configuration; its answers may not follow the DNS rules";
    }
    return r;
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
    first_sync_done_ = true;
}

void DnsmasqManager::sync_from_config(const Config& config, const CacheManager& cache,
                                      bool explicit_apply, const std::string& reason) {
    try {
        const DnsConfig dns = config.dns.value_or(DnsConfig{});
        // The registry resolves type=keenetic servers and is only needed (and
        // allowed to fail) when the integration is on.
        const DnsServerRegistry registry(dnsmasq_integration_enabled(config) ? dns : DnsConfig{});
        if (balancer_groups_) {
            balancer_groups_(registry.balancer_groups());
        }
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
        });
        publish_error(DnsmasqErrorCode::NoHook, "no dnsmasq hook for this platform");
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

    // OpenWrt moves the router's own upstream servers aside only while
    // keen-pbr provides the default ones; the hook gets that as an argument.
    // The hash changes whenever this flips, which re-runs the hook.
    default_upstreams_ = !stats.fallback.empty();

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

    configured_selector_ = configured_dnsmasq_selector(config);
    if (!refresh_detection_locked(hash)) {
        if (explicit_now) {
            apply_blocked_ = true;
        }
        return;
    }
    const DnsmasqInstance* inst = detection_.selected_instance();

    // dnsmasq may already serve this exact config (keen-pbr restarted, or
    // dnsmasq started first): then there is nothing to install or restart.
    if (!explicit_now && !apply_blocked_) {
        // Same config as before (e.g. lists autoupdate): only repair dnsmasq
        // if it lost the config, within the repair budget.
        evaluate_locked(hash, probe_once());
        return;
    }
    if (inst->evidence == EndpointEvidence::StampVerified && inst->stamp) {
        log.info("dnsmasq {} already serves keen-pbr config (hash {}); not restarting",
                 inst->selector.str(), hash);
        mark_in_sync(*inst->stamp, /*external=*/false);
        return;
    }

    if (!ensure_ready_to_apply_locked(hash, /*allow_cached_preflight=*/false)) {
        apply_blocked_ = true;
        return;
    }
    apply_blocked_ = false;
    apply_and_confirm(hash, ApplyContext{false, reason});
}

bool DnsmasqManager::refresh_detection_locked(const std::string& expected_hash) {
    DnsmasqDetectOptions options;
    options.configured_selector = configured_selector_;
    options.expected_hash = expected_hash;
    options.previous_endpoint = endpoint_;
    detection_ = detect_(options);
    const DnsmasqInstance* inst = detection_.selected_instance();
    // Read guarded state here: thread-safety analysis does not see the lock inside the lambda.
    const std::size_t instance_count = detection_.instances.size();
    std::vector<std::string> warnings = detection_.warnings;
    const auto selection = detection_.selection;

    update_status([&](DnsmasqStatus& s) {
        s.instances = instance_count;
        s.warnings = std::move(warnings);
        if (inst) {
            s.warnings.insert(s.warnings.end(), inst->warnings.begin(), inst->warnings.end());
        }
        s.selection = selection;
        s.selector = inst ? inst->selector.str() : std::string{};
        s.endpoint = inst ? inst->endpoint : std::nullopt;
        s.endpoint_evidence = inst ? inst->evidence : EndpointEvidence::None;
        s.pid = (inst && inst->process) ? std::optional<pid_t>(inst->process->pid) : std::nullopt;
        s.dnsmasq_alive = inst ? (inst->process ? DnsmasqLiveness::Alive : DnsmasqLiveness::Dead)
                               : DnsmasqLiveness::Unknown;
    });
    for (const std::string& warning : status().warnings) {
        if (logged_warnings_.insert(warning).second) {
            Logger::instance().warn("dnsmasq: {}", warning);
        }
    }

    if (detection_.error != DnsmasqErrorCode::None) {
        endpoint_.reset();
        publish_error(detection_.error, detection_.message);
        return false;
    }
    selected_selector_ = inst->selector;
    endpoint_ = inst->endpoint;
    if (inst->problem != DnsmasqErrorCode::None) {
        publish_error(inst->problem, inst->problem_detail);
        return false;
    }
    return true;
}

bool DnsmasqManager::ensure_ready_to_apply_locked(const std::string& hash,
                                                  bool allow_cached_preflight) {
    auto& log = Logger::instance();
    const DnsmasqInstance* inst = detection_.selected_instance();
    if (inst == nullptr) {
        publish_error(DnsmasqErrorCode::NoDnsInstance, "no dnsmasq instance is selected");
        return false;
    }
    if (inst->hook.preflight_skip) {
        update_status([](DnsmasqStatus& s) {
            s.capability = DnsmasqCapabilityStatus::NotChecked;
            s.preflight = "skipped";
        });
        return true;
    }
    const std::string path = inst->process ? inst->process->exe : detection_.inspect.binary;
    const auto identity = dnsmasq_binary_identity(path);
    if (!identity || !capability_identity_ || !(*identity == *capability_identity_)) {
        capability_ = capability_fn_(path);
        capability_identity_ = identity;
        log.info("dnsmasq capability test for {}: {}", path,
                 capability_status_name(capability_.status));
    }
    const DnsmasqCapabilityStatus capability_status = capability_.status;
    update_status([&](DnsmasqStatus& s) { s.capability = capability_status; });
    if (capability_.status != DnsmasqCapabilityStatus::Ok) {
        publish_error(capability_.code,
                      "dnsmasq " + path + " cannot be used: " + capability_.detail);
        return false;
    }
    if (allow_cached_preflight && preflight_ok_hash_ == hash) {
        return true;
    }
    const DnsmasqPreflightResult pf = preflight_fn_(*inst);
    if (!pf.ok) {
        update_status([](DnsmasqStatus& s) { s.preflight = "failed"; });
        publish_error(DnsmasqErrorCode::PreflightFailed,
                      "dnsmasq --test rejected the configuration with the keen-pbr "
                      "conf-script entry; dnsmasq was not restarted: " + pf.detail);
        return false;
    }
    preflight_ok_hash_ = hash;
    update_status([&](DnsmasqStatus& s) { s.preflight = pf.skipped ? "skipped" : "ok"; });
    log.info("dnsmasq preflight passed for {} in {} ms", inst->selector.str(),
             pf.duration.count());
    return true;
}

void DnsmasqManager::publish_error(DnsmasqErrorCode code, const std::string& message) {
    if (last_logged_problem_ != message) {
        last_logged_problem_ = message;
        Logger::instance().warn("dnsmasq integration: {}: {}", dnsmasq_error_code_name(code),
                                message);
    }
    publish_state(DnsmasqSyncState::Error, message);
    update_status([&](DnsmasqStatus& s) { s.error_code = code; });
}

DnsmasqManager::ProbeObservation DnsmasqManager::probe_once() {
    ProbeObservation observation;
    if (!endpoint_) {
        observation.raw.status = DnsTxtProbeStatus::QueryFailed;
        observation.raw.error = "no dnsmasq DNS endpoint is known";
    } else {
        observation.raw = probe_(*endpoint_);
    }
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
        if (state == DnsmasqSyncState::Ok) {
            status.error_code = DnsmasqErrorCode::None;
        } else if (state == DnsmasqSyncState::Reconciling) {
            status.error_code = DnsmasqErrorCode::StampMismatch;
        }
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
    last_logged_problem_.clear();
    apply_blocked_ = false;

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
    // The stamp may come from detection rather than from probe_once().
    const std::int64_t checked_ts = unix_timestamp_now_seconds();
    update_status([&](DnsmasqStatus& status) {
        status.probe_state = DnsmasqProbeState::Ok;
        status.loaded_hash = stamp.hash;
        status.loaded_boottime_ms = stamp.boottime_ms;
        status.loaded_ts = stamp.unix_ts;
        status.last_check_ts = checked_ts;
    });
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
               " missing); dnsmasq did not run the keen-pbr conf-script";
    case DnsmasqProbeState::QueryFailed:
        return "dnsmasq did not answer the config check on " +
               (endpoint_ ? endpoint_->str() : std::string("(unknown endpoint)")) + ": " +
               obs.raw.error;
    case DnsmasqProbeState::Invalid:
        return "invalid " + std::string(kDnsmasqStampDomain) + " TXT record: " + obs.raw.txt;
    case DnsmasqProbeState::Ok:
        break;
    case DnsmasqProbeState::NotChecked:
        return "dnsmasq config check did not run";
    }
    if (obs.stamp->hash == kDnsmasqOffStampHash) {
        return "dnsmasq restarted but keen-pbr printed its fallback output instead of the "
               "generated config; see the keen-pbr log for generate-resolver-config";
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

    if (!selected_selector_) {
        publish_error(DnsmasqErrorCode::NoDnsInstance, "no dnsmasq instance is selected");
        return;
    }
    const std::string failure = run_hook({"apply", selected_selector_->str(),
                                          default_upstreams_ ? "--default-upstreams=yes"
                                                             : "--default-upstreams=no"});
    if (!failure.empty()) {
        log.error("dnsmasq hook apply failed: {}", failure);
        publish_error(DnsmasqErrorCode::ApplyFailed, failure);
        return;
    }

    // dnsmasq runs the conf-script while it restarts; poll its stamp until it
    // serves this hash and was (re)started after we began applying.  An
    // unanswered query keeps polling: dnsmasq is restarting.
    const auto started = std::chrono::steady_clock::now();
    std::chrono::milliseconds waited{0};
    ProbeObservation last;
    int misses = 0;
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
        ++misses;
        if (misses % 4 == 0) {
            // dnsmasq may come back on another address after the restart.
            DnsmasqDetectOptions options;
            options.configured_selector = configured_selector_;
            options.expected_hash = hash;
            options.previous_endpoint = endpoint_;
            options.probe = true;
            const DnsmasqDetection detection = detect_(options);
            const DnsmasqInstance* found = detection.selected_instance();
            if (found && selected_selector_ && found->selector == *selected_selector_ &&
                found->endpoint) {
                endpoint_ = found->endpoint;
            }
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
    publish_error(DnsmasqErrorCode::ConfigNotLoaded, message);
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
        publish_error(DnsmasqErrorCode::RepairPaused, message);
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

    if (!ensure_ready_to_apply_locked(hash, /*allow_cached_preflight=*/true)) {
        return;
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
        // Never restart here (decision D2): re-detect, report, and wait.
        if (!refresh_detection_locked(hash)) {
            in_sync_since_ms_ = -1;
            return;
        }
        observed = probe_once();  // endpoint_ may have changed
        if (observed.state == DnsmasqProbeState::QueryFailed) {
            in_sync_since_ms_ = -1;
            publish_error(DnsmasqErrorCode::ProbeFailed,
                          "dnsmasq " + selected_selector_->str() +
                              " is running but does not answer the config check on " +
                              (endpoint_ ? endpoint_->str() : std::string("(unknown endpoint)")) +
                              ": " + observed.raw.error + "; not restarting it");
            return;
        }
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

    if (observed.stamp && observed.stamp->hash == kDnsmasqOffStampHash) {
        problem = "dnsmasq runs the output for switched-off management, not the generated config";
    } else if (observed.state == DnsmasqProbeState::Invalid) {
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
    if (!endpoint_ || apply_blocked_) {
        if (!refresh_detection_locked(expected_hash_)) {
            return;
        }
        const DnsmasqInstance* inst = detection_.selected_instance();
        if (inst->evidence == EndpointEvidence::StampVerified && inst->stamp) {
            mark_in_sync(*inst->stamp, /*external=*/false);
            return;
        }
        if (apply_blocked_) {
            if (!ensure_ready_to_apply_locked(expected_hash_, false)) {
                return;
            }
            apply_blocked_ = false;
            reset_repair_budget();
            apply_and_confirm(expected_hash_, ApplyContext{false, "dnsmasq became available"});
            return;
        }
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

// Asks the hook to `remove` keen-pbr from one dnsmasq instance (restart, and on
// OpenWrt restore the moved servers).  Without `known_target` the instance is
// found by detection.  `require_managed_output`: only act when that instance
// visibly runs the generated config (the startup case).  Returns the failure
// text, empty on success or when there was nothing to do.
std::string DnsmasqManager::remove_from_dnsmasq(std::optional<std::string> known_target,
                                                bool require_managed_output) {
    std::optional<std::string> target = std::move(known_target);
    if (!target) {
        DnsmasqDetectOptions options;
        options.configured_selector = configured_selector_;
        options.probe = require_managed_output;
        options.probe_all = false;
        const DnsmasqDetection det = detect_(options);
        if (require_managed_output) {
            const DnsmasqInstance* inst = det.selected_instance();
            if (det.error == DnsmasqErrorCode::None && inst != nullptr && inst->stamp &&
                inst->stamp->hash != kDnsmasqOffStampHash) {
                Logger::instance().info(
                    "dnsmasq {} still runs the generated keen-pbr config but management "
                    "is off; restarting it",
                    inst->selector.str());
                target = inst->selector.str();
            }
        } else {
            std::vector<std::string> installed;
            for (const DnsmasqInstance& instance : det.instances) {
                if (instance.hook.dropin_state == DropinState::Current) {
                    installed.push_back(instance.selector.str());
                }
            }
            if (installed.size() == 1) {
                target = installed[0];
            } else if (installed.size() > 1) {
                std::string joined;
                for (const std::string& item : installed) {
                    joined += (joined.empty() ? "" : ", ") + item;
                }
                Logger::instance().warn(
                    "dnsmasq management disabled; keen-pbr drop-in is installed in {}; "
                    "delete the drop-in from the instances keen-pbr should not use and "
                    "restart them, or set dns.dnsmasq_instance", joined);
            }
        }
    }
    if (!target) {
        return {};
    }
    std::string failure = run_hook({"remove", *target});
    // Exit 3: the instance no longer exists, nothing to remove.
    if (failure.rfind("hook exited 3", 0) == 0) {
        failure.clear();
    }
    if (!failure.empty()) {
        Logger::instance().error("dnsmasq hook remove failed: {}", failure);
    }
    return failure;
}

void DnsmasqManager::sync_disabled() {
    // The integration is off.  The conf-script stays in dnsmasq's config and
    // prints the fallback servers while it is off, so nothing is changed on
    // disk.  Only a switch-off inside this daemon asks the hook to restart
    // dnsmasq (OpenWrt also returns the servers it moved aside).  At startup
    // dnsmasq is left alone unless it visibly still runs the generated config
    // of an earlier "on" state: then one restart brings it in line.
    std::string failure;
    if (hook_path_.empty()) {
        // Nothing to ask.
    } else if (previous_mode_ == ResolverIntegrationMode::DNSMASQ) {
        Logger::instance().info("Restarting dnsmasq: keen-pbr dnsmasq management disabled");
        failure = remove_from_dnsmasq(selected_selector_ ? std::optional<std::string>(selected_selector_->str())
                                                         : std::nullopt,
                                      /*require_managed_output=*/false);
    } else if (!first_sync_done_) {
        failure = remove_from_dnsmasq(std::nullopt, /*require_managed_output=*/true);
    }
    selected_selector_.reset();
    endpoint_.reset();
    detection_ = DnsmasqDetection{};
    apply_blocked_ = false;
    preflight_ok_hash_.clear();
    last_logged_problem_.clear();
    logged_warnings_.clear();
    capability_ = DnsmasqCapabilityResult{};
    capability_identity_.reset();
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
