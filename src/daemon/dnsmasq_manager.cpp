#include "dnsmasq_manager.hpp"

#include "../dns/dnsmasq_gen.hpp"
#include "../log/logger.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <ostream>
#include <streambuf>
#include <system_error>

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxHookOutputBytes = 2048;

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

} // namespace

DnsmasqManager::DnsmasqManager(std::string hook_path, DnsmasqExecFn exec)
    : hook_path_(std::move(hook_path)),
      exec_(exec ? std::move(exec) : DnsmasqExecFn(default_exec)) {}

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
                          ListStreamer& streamer) {
    KPBR_LOCK_GUARD(sync_mutex_);
    const ResolverIntegrationMode mode = effective_resolver_integration(config);
    try {
        if (mode == ResolverIntegrationMode::DNSMASQ) {
            sync_dnsmasq(config, registry, streamer);
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

void DnsmasqManager::sync_from_config(const Config& config, const CacheManager& cache) {
    try {
        const DnsConfig dns = config.dns.value_or(DnsConfig{});
        // The registry resolves type=keenetic servers and is only needed (and
        // allowed to fail) when the integration is on.
        const DnsServerRegistry registry(
            effective_resolver_integration(config) == ResolverIntegrationMode::DNSMASQ
                ? dns
                : DnsConfig{});
        ListStreamer streamer(cache);
        sync(config, registry, streamer);
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
                                  ListStreamer& streamer) {
    auto& log = Logger::instance();
    if (hook_path_.empty()) {
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

    if (hash == last_applied_hash_) {
        update_status([&](DnsmasqStatus& status) {
            status.mode = ResolverIntegrationMode::DNSMASQ;
            status.state = DnsmasqSyncState::Ok;
            status.config_hash = hash;
            status.last_error.clear();
            status.rules = stats.rules;
            status.domains = stats.domains;
        });
        return;
    }

    update_status([&](DnsmasqStatus& status) {
        status.mode = ResolverIntegrationMode::DNSMASQ;
        status.state = DnsmasqSyncState::Applying;
        status.rules = stats.rules;
        status.domains = stats.domains;
    });
    log.info("Installing dnsmasq config ({} rule(s), {} domain(s), hash {})",
             stats.rules, stats.domains, hash);
    const std::string failure = run_hook({"apply"});
    if (!failure.empty()) {
        log.error("dnsmasq hook apply failed: {}", failure);
        update_status([&](DnsmasqStatus& status) {
            status.state = DnsmasqSyncState::Error;
            status.last_error = failure;
        });
        return;
    }

    last_applied_hash_ = hash;
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    update_status([&](DnsmasqStatus& status) {
        status.state = DnsmasqSyncState::Ok;
        status.config_hash = hash;
        status.last_apply_ts = now;
        status.last_error.clear();
    });
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
        last_applied_hash_.clear();
    }

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
                                  const DnsmasqPostFn& post) {
    {
        KPBR_LOCK_GUARD(request_mutex_);
        pending_config_ = std::move(config);
        pending_cache_ = &cache;
        if (worker_active_) {
            return;  // the active run picks up the latest config
        }
        worker_active_ = true;
    }
    if (!post([this] { run_pending_requests(); })) {
        Logger::instance().warn("dnsmasq config sync not scheduled: executor unavailable");
        KPBR_LOCK_GUARD(request_mutex_);
        worker_active_ = false;
    }
}

void DnsmasqManager::run_pending_requests() {
    while (true) {
        Config config;
        const CacheManager* cache = nullptr;
        {
            KPBR_LOCK_GUARD(request_mutex_);
            if (!pending_config_.has_value()) {
                worker_active_ = false;
                return;
            }
            config = std::move(*pending_config_);
            pending_config_.reset();
            cache = pending_cache_;
        }
        sync_from_config(config, *cache);
    }
}

} // namespace keen_pbr3
