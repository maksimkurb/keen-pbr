#include "daemon.hpp"
#include <algorithm>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <signal.h>
#include <sstream>
#include <shared_mutex>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../dns/dnsmasq_gen.hpp"
#include "../dns/dns_probe_server.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dns_server.hpp"
#include "../dns/dns_txt_client.hpp"
#include "../firewall/firewall.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_set_usage.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../routing/urltest_manager.hpp"
#include "../config/routing_state.hpp"
#include "../config/addr_spec.hpp"
#include "../util/cron.hpp"
#include "scheduler.hpp"
#include "system_resolver_hook.hpp"

#ifndef KEEN_PBR_FRONTEND_ROOT
#define KEEN_PBR_FRONTEND_ROOT "/usr/share/keen-pbr/frontend"
#endif

#ifdef WITH_API
#include "../api/handlers.hpp"
#include "../api/server.hpp"
#include "../api/sse_broadcaster.hpp"
#endif

namespace keen_pbr3 {

// Helper to get tag from an outbound
std::string get_outbound_tag(const Outbound& ob) {
    return ob.tag;
}

// Find an outbound by tag, returning pointer or nullptr
const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                               const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (ob.tag == tag) {
            return &ob;
        }
    }
    return nullptr;
}

Daemon::Daemon(Config config,
               std::string config_path,
               DaemonOptions opts,
               HookCommandExecutor hook_command_executor)
    : config_(std::move(config))
    , config_path_(std::move(config_path))
    , opts_(std::move(opts))
    , cache_(config_.daemon.value_or(DaemonConfig{}).cache_dir.value_or("/var/cache/keen-pbr"))
    , firewall_(create_firewall("auto"))
    , netlink_()
    , route_table_(netlink_)
    , policy_rules_(netlink_)
    , firewall_state_()
    , url_tester_()
    , outbound_marks_(allocate_outbound_marks(config_.fwmark.value_or(FwmarkConfig{}),
                                             config_.outbounds.value_or(std::vector<Outbound>{})))
    , hook_command_executor_(std::move(hook_command_executor))
{
    if (!hook_command_executor_) {
        hook_command_executor_ = default_hook_command_executor;
    }

    // Initialize epoll
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
    }

    setup_signals();
    setup_control_channel();

    // Set outbound marks in firewall state
    firewall_state_.set_outbound_marks(outbound_marks_);

    // Ensure cache directory exists
    cache_.ensure_dir();

    // Create scheduler (needs Daemon& for epoll fd registration)
    scheduler_ = std::make_unique<Scheduler>(*this);

    // UrltestManager created later during startup (register_urltest_outbounds)

    // RoutingHealthChecker depends on firewall_, firewall_state_, route_table_, policy_rules_, netlink_
    // All are initialized by this point in the member initializer list
    routing_health_checker_ = std::make_unique<RoutingHealthChecker>(
        *firewall_, firewall_state_, route_table_, policy_rules_, netlink_);

#ifdef WITH_API
    dns_test_broadcaster_ = std::make_unique<SseBroadcaster>();
#endif
}

Daemon::~Daemon() {
    if (control_fd_ >= 0) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, control_fd_, nullptr);
        close(control_fd_);
    }
    if (signal_fd_ >= 0) {
        // Remove from epoll before closing
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, signal_fd_, nullptr);
        close(signal_fd_);
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }

    // Restore default signal disposition
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGHUP);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

void Daemon::setup_control_channel() {
    control_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (control_fd_ < 0) {
        throw DaemonError("eventfd failed: " + std::string(strerror(errno)));
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = control_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, control_fd_, &ev) < 0) {
        throw DaemonError("epoll_ctl add control_fd failed: " + std::string(strerror(errno)));
    }
}

void Daemon::wake_control_loop() {
    const uint64_t inc = 1;
    ssize_t n = write(control_fd_, &inc, sizeof(inc));
    if (n < 0 && errno != EAGAIN) {
        throw DaemonError("eventfd write failed: " + std::string(strerror(errno)));
    }
}

void Daemon::enqueue_control_task(std::function<void()> task,
                                  bool wait_for_completion) {
    if (!task) {
        return;
    }

    if (!event_loop_active_.load(std::memory_order_acquire) ||
        event_loop_thread_id_ == std::thread::id{}) {
        task();
        return;
    }

    if (event_loop_thread_id_ == std::this_thread::get_id()) {
        task();
        return;
    }

    if (wait_for_completion) {
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();
        {
            std::lock_guard<std::mutex> lock(control_tasks_mutex_);
            control_tasks_.push_back([cmd = std::move(task), done]() mutable {
                try {
                    cmd();
                    done->set_value();
                } catch (...) {
                    done->set_exception(std::current_exception());
                }
            });
        }
        wake_control_loop();
        fut.get();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(control_tasks_mutex_);
        control_tasks_.push_back(std::move(task));
    }
    wake_control_loop();
}


void Daemon::enqueue_control_command(std::function<void()> command,
                                     bool wait_for_completion) {
    enqueue_control_task(std::move(command), wait_for_completion);
}
void Daemon::handle_control_commands() {
    uint64_t counter = 0;
    while (read(control_fd_, &counter, sizeof(counter)) > 0) {
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        throw DaemonError("eventfd read failed: " + std::string(strerror(errno)));
    }

    std::vector<std::function<void()>> commands;
    {
        std::lock_guard<std::mutex> lock(control_tasks_mutex_);
        commands.swap(control_tasks_);
    }

    for (auto& command : commands) {
        command();
    }
}

void Daemon::setup_signals() {
    // Block SIGTERM, SIGINT, SIGUSR1, SIGHUP so they can be handled via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        throw DaemonError("sigprocmask failed: " + std::string(strerror(errno)));
    }

    // Create signalfd for the blocked signals
    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        throw DaemonError("signalfd failed: " + std::string(strerror(errno)));
    }

    // Add signalfd to epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = signal_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) < 0) {
        throw DaemonError("epoll_ctl add signalfd failed: " + std::string(strerror(errno)));
    }
}

void Daemon::handle_signal() {
    struct signalfd_siginfo info{};
    ssize_t n = read(signal_fd_, &info, sizeof(info));
    if (n != sizeof(info)) {
        return;
    }

    switch (info.ssi_signo) {
    case SIGTERM:
    case SIGINT:
        running_ = false;
        break;
    case SIGUSR1:
        handle_sigusr1();
        break;
    case SIGHUP:
        handle_sighup();
        break;
    default:
        break;
    }
}

void Daemon::handle_sigusr1() {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    auto& log = Logger::instance();
    log.info("SIGUSR1: verifying routing tables and triggering URL tests...");

    // Re-add static routing tables/ip rules in case they were lost
    try {
        route_table_.clear();
        policy_rules_.clear();
        setup_static_routing();
        log.info("SIGUSR1: static routing tables verified.");
    } catch (const std::exception& e) {
        log.error("SIGUSR1: error verifying routing: {}", e.what());
    }

    // Trigger immediate URL tests for all urltest outbounds
    if (urltest_manager_) {
        for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
            if (ob.type == OutboundType::URLTEST) {
                urltest_manager_->trigger_immediate_test(ob.tag);
            }
        }
    }

    log.info("SIGUSR1: complete.");
}

void Daemon::handle_sighup() {
    auto& log = Logger::instance();
    log.info("SIGHUP: full reload starting...");
    try {
        reload_from_disk();
        log.info("SIGHUP: full reload complete.");
    } catch (const std::exception& e) {
        log.error("SIGHUP: reload failed: {}", e.what());
    }
}

void Daemon::run_system_resolver_hook_reload() {
    auto& log = Logger::instance();

    std::string command;
    int exit_code = 0;
    const bool ok = execute_system_resolver_reload_hook(
        config_,
        hook_command_executor_,
        command,
        exit_code);

    if (command.empty()) {
        return;
    }

    if (!ok) {
        log.warn("System resolver reload hook failed (exit code: {}): {}",
                 exit_code,
                 command);
        return;
    }

    log.info("System resolver reload hook complete: {}", command);
}

void Daemon::add_fd(int fd, uint32_t events, FdCallback cb) {
    enqueue_control_task([this, fd, events, cb = std::move(cb)]() mutable {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            throw DaemonError("epoll_ctl add fd failed: " + std::string(strerror(errno)));
        }

        std::lock_guard<std::mutex> lock(fd_entries_mutex_);
        fd_entries_.push_back({fd, std::move(cb)});
    }, true);
}

void Daemon::remove_fd(int fd) {
    enqueue_control_task([this, fd]() {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

        std::lock_guard<std::mutex> lock(fd_entries_mutex_);
        fd_entries_.erase(
            std::remove_if(fd_entries_.begin(), fd_entries_.end(),
                           [fd](const FdEntry& e) { return e.fd == fd; }),
            fd_entries_.end());
    }, true);
}

void Daemon::run() {
    // --- Startup sequence ---
    auto& log = Logger::instance();

    write_pid_file();

    log.info("Loading lists...");
    download_uncached_lists();

    setup_static_routing();
    log.info("Static routing tables and ip rules installed.");

    register_urltest_outbounds();

    apply_firewall();
    log.info("Firewall rules and routing applied.");

    schedule_lists_autoupdate();

    update_resolver_config_hash();
    update_resolver_config_hash_actual();

    setup_dns_probe();

#ifdef WITH_API
    setup_api();
#endif

    log.info("Daemon running. PID: {}", getpid());

    // --- Event loop ---
    running_ = true;
    event_loop_thread_id_ = std::this_thread::get_id();
    event_loop_active_.store(true, std::memory_order_release);

    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw DaemonError("epoll_wait failed: " + std::string(strerror(errno)));
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handle_signal();
                continue;
            }
            if (fd == control_fd_) {
                handle_control_commands();
                continue;
            }

            // Dispatch to registered fd callbacks
            FdCallback callback;
            {
                std::lock_guard<std::mutex> lock(fd_entries_mutex_);
                for (auto& entry : fd_entries_) {
                    if (entry.fd == fd) {
                        callback = entry.callback;
                        break;
                    }
                }
            }
            if (callback) {
                callback(events[i].events);
            }
        }
    }

    event_loop_active_.store(false, std::memory_order_release);
    event_loop_thread_id_ = std::thread::id{};

    // --- Shutdown sequence ---
    log.info("Shutting down...");

#ifdef WITH_API
    if (dns_test_broadcaster_) {
        dns_test_broadcaster_->close_all();
    }
    if (api_server_) {
        api_server_->stop();
    }
#endif

    teardown_dns_probe();

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    scheduler_->cancel_all();
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();
    remove_pid_file();
}

void Daemon::stop() {
    running_ = false;
}

bool Daemon::running() const {
    return running_;
}

void Daemon::write_pid_file() {
    const auto pid_file = config_.daemon.value_or(DaemonConfig{}).pid_file.value_or("");
    if (pid_file.empty()) return;
    std::filesystem::create_directories(std::filesystem::path(pid_file).parent_path());
    std::ofstream ofs(pid_file);
    if (!ofs.is_open()) {
        throw DaemonError("Cannot write PID file: " + pid_file);
    }
    ofs << getpid() << "\n";
}

void Daemon::remove_pid_file() {
    const auto pid_file = config_.daemon.value_or(DaemonConfig{}).pid_file.value_or("");
    if (!pid_file.empty()) {
        std::filesystem::remove(pid_file);
    }
}

void Daemon::setup_static_routing() {
    populate_routing_state(
        config_,
        outbound_marks_,
        route_table_,
        policy_rules_,
        [this](const Outbound& outbound) {
            return is_interface_outbound_reachable(outbound, netlink_);
        },
        &firewall_state_.get_urltest_selections());
}

void Daemon::apply_firewall() {
    ListStreamer list_streamer(cache_);
    auto rule_states =
        build_fw_rule_states(config_, outbound_marks_, &firewall_state_.get_urltest_selections());

    // Clean existing firewall state before rebuilding
    firewall_->cleanup();

    const auto& all_outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config_.lists ? *config_.lists : empty_lists;
    const auto& route_rules = config_.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});
    std::map<std::string, ListSetUsage> list_usage_cache;

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];
        const RuleState& rs = rule_states[rule_idx];

        if (rs.action_type == RuleActionType::Skip) {
            continue;
        }

        const bool is_blackhole = (rs.action_type == RuleActionType::Drop);

        // Create ipsets and stream entries for each list in the rule
        for (const auto& list_name : rule.list) {
            auto list_cfg_it = lists_map.find(list_name);
            if (list_cfg_it == lists_map.end()) continue;

            const auto& list_cfg = list_cfg_it->second;
            auto usage_it = list_usage_cache.find(list_name);
            if (usage_it == list_usage_cache.end()) {
                usage_it = list_usage_cache.emplace(
                    list_name,
                    analyze_list_set_usage(list_name, list_cfg, list_streamer)).first;
            }
            const auto& usage = usage_it->second;

            // Static sets: permanent IP/CIDR entries (no timeout)
            const std::string set4 = "kpbr4_"  + list_name;
            const std::string set6 = "kpbr6_"  + list_name;
            // Dynamic sets: dnsmasq-resolved entries (TTL from ttl_ms)
            const std::string set4d = "kpbr4d_" + list_name;
            const std::string set6d = "kpbr6d_" + list_name;

            if (usage.has_static_entries) {
                firewall_->create_ipset(set4, AF_INET, 0);
                firewall_->create_ipset(set6, AF_INET6, 0);

                auto loader4 = firewall_->create_batch_loader(set4);
                auto loader6 = firewall_->create_batch_loader(set6);
                FunctionalVisitor splitter([&](EntryType type, std::string_view entry) {
                    if (type == EntryType::Domain) return;
                    bool is_v6 = entry.find(':') != std::string_view::npos;
                    if (is_v6) loader6->on_entry(type, entry);
                    else       loader4->on_entry(type, entry);
                });
                list_streamer.stream_list(list_name, list_cfg, splitter);
                loader4->finish();
                loader6->finish();
            }

            if (usage.has_domain_entries) {
                firewall_->create_ipset(set4d, AF_INET, usage.dynamic_timeout);
                firewall_->create_ipset(set6d, AF_INET6, usage.dynamic_timeout);
            }

            // Build proto/port/addr filter from route rule.
            // Strip leading '!' to extract negation flags.
            auto strip_neg = [](const std::string& s) -> std::pair<std::string, bool> {
                if (!s.empty() && s[0] == '!') return {s.substr(1), true};
                return {s, false};
            };

            ProtoPortFilter filter;
            filter.proto = rule.proto.value_or("");

            {
                auto [port, neg]   = strip_neg(rule.src_port.value_or(""));
                filter.src_port        = port;
                filter.negate_src_port = neg;
            }
            {
                auto [port, neg]   = strip_neg(rule.dest_port.value_or(""));
                filter.dst_port        = port;
                filter.negate_dst_port = neg;
            }
            {
                AddrSpec s = parse_addr_spec(rule.src_addr.value_or(""));
                filter.negate_src_addr = s.negate;
                filter.src_addr        = std::move(s.addrs);
            }
            {
                AddrSpec s = parse_addr_spec(rule.dest_addr.value_or(""));
                filter.negate_dst_addr = s.negate;
                filter.dst_addr        = std::move(s.addrs);
            }

            // Create mark or drop rules for both static and dynamic sets (OR semantics)
            if (is_blackhole) {
                if (usage.has_static_entries) {
                    firewall_->create_drop_rule(set4, filter);
                    firewall_->create_drop_rule(set6, filter);
                }
                if (usage.has_domain_entries) {
                    firewall_->create_drop_rule(set4d, filter);
                    firewall_->create_drop_rule(set6d, filter);
                }
            } else if (rs.fwmark != 0) {
                if (usage.has_static_entries) {
                    firewall_->create_mark_rule(set4, rs.fwmark, filter);
                    firewall_->create_mark_rule(set6, rs.fwmark, filter);
                }
                if (usage.has_domain_entries) {
                    firewall_->create_mark_rule(set4d, rs.fwmark, filter);
                    firewall_->create_mark_rule(set6d, rs.fwmark, filter);
                }
            }
        }
    }

    // DNS server detour: mark port-53 traffic for servers with a detour outbound
    if (config_.dns.has_value()) {
        const auto& dns_servers =
            config_.dns->servers.value_or(std::vector<DnsServer>{});
        const DnsServerRegistry dns_registry(config_.dns.value_or(DnsConfig{}));
        for (const auto& srv : dns_servers) {
            if (!srv.detour.has_value()) continue;

            const Outbound* detour_ob = find_outbound(all_outbounds, srv.detour.value());
            if (!detour_ob) continue;

            // Resolve URLTEST → selected child
            std::string effective_tag = detour_ob->tag;
            if (detour_ob->type == OutboundType::URLTEST) {
                auto selections = firewall_state_.get_urltest_selections();
                auto sel_it = selections.find(effective_tag);
                if (sel_it != selections.end() && !sel_it->second.empty()) {
                    const Outbound* child = find_outbound(all_outbounds, sel_it->second);
                    if (child) effective_tag = child->tag;
                }
            }

            auto mark_it = outbound_marks_.find(effective_tag);
            if (mark_it == outbound_marks_.end()) continue;

            const DnsServerConfig* resolved_server = dns_registry.get_server(srv.tag);
            if (!resolved_server) {
                throw DaemonError("DNS server tag not found during detour setup: " + srv.tag);
            }
            ProtoPortFilter filter;
            filter.proto    = "tcp/udp";
            filter.dst_port = std::to_string(resolved_server->port);
            filter.dst_addr = {resolved_server->resolved_ip};
            firewall_->create_direct_mark_rule(mark_it->second, filter);
        }
    }

    firewall_->apply();
    firewall_state_.set_rules(std::move(rule_states));
}

void Daemon::download_uncached_lists() {
    for (const auto& [name, list_cfg] : config_.lists.value_or(std::map<std::string, ListConfig>{})) {
        if (!list_cfg.url.has_value() || cache_.has_cache(name)) continue;

        uint32_t mark = 0;
        if (list_cfg.detour.has_value()) {
            auto it = outbound_marks_.find(*list_cfg.detour);
            if (it != outbound_marks_.end()) {
                mark = it->second;
            } else {
                Logger::instance().warn(
                    "List '{}': detour outbound '{}' not found, using default routing",
                    name, *list_cfg.detour);
            }
        }
        cache_.set_fwmark(mark);

        try {
            cache_.download(name, *list_cfg.url);
        } catch (const std::exception& e) {
            Logger::instance().warn("Failed to download list '{}': {}", name, e.what());
        }
    }
    cache_.set_fwmark(0);
}

void Daemon::register_urltest_outbounds() {
    urltest_manager_ = std::make_unique<UrltestManager>(
        url_tester_, outbound_marks_, *scheduler_,
        [this](const std::string& urltest_tag, const std::string& new_child_tag) {
            auto& log = Logger::instance();
            log.info("Urltest '{}' selected outbound: '{}'", urltest_tag, new_child_tag);
            firewall_state_.set_urltest_selection(urltest_tag, new_child_tag);
            try {
                route_table_.clear();
                policy_rules_.clear();
                setup_static_routing();
                apply_firewall();
                log.info("Routing and firewall rebuilt after urltest change.");
            } catch (const std::exception& e) {
                log.error("Error rebuilding routing/firewall after urltest change: {}", e.what());
            }
        });

    for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
        if (ob.type == OutboundType::URLTEST) {
            urltest_manager_->register_urltest(ob);
        }
    }
}

void Daemon::schedule_lists_autoupdate() {
    if (!config_.lists_autoupdate) return;
    if (!config_.lists_autoupdate->enabled.value_or(false)) return;
    const auto& expr = config_.lists_autoupdate->cron.value_or("");
    auto next = cron_next(expr);
    auto delay = std::chrono::duration_cast<std::chrono::seconds>(
        next - std::chrono::system_clock::now());
    if (delay.count() < 1) delay = std::chrono::seconds{1};
    lists_autoupdate_task_id_ = scheduler_->schedule_oneshot(delay, [this]() {
        refresh_lists_and_maybe_reload();
    });
    Logger::instance().info("Lists autoupdate scheduled (next: ~{}s)", delay.count());
}

void Daemon::refresh_lists_and_maybe_reload() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    // Build set of lists referenced by any route rule
    std::set<std::string> route_lists;
    for (const auto& rule : config_.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{}))
        for (const auto& ln : rule.list)
            route_lists.insert(ln);

    bool any_relevant_changed = false;
    for (const auto& [name, list_cfg] : config_.lists.value_or(std::map<std::string, ListConfig>{})) {
        if (!list_cfg.url.has_value()) continue;
        try {
            bool changed = cache_.download(name, *list_cfg.url);
            if (changed) {
                log.info("Lists autoupdate: list '{}' updated", name);
                if (route_lists.count(name))
                    any_relevant_changed = true;
            }
        } catch (const std::exception& e) {
            log.warn("Lists autoupdate: failed to refresh list '{}': {}", name, e.what());
        }
    }

    if (any_relevant_changed) {
        log.info("Lists autoupdate: relevant list(s) changed, triggering reload");
        try { reload_from_disk(); }
        catch (const std::exception& e) {
            log.error("Lists autoupdate: reload failed: {}", e.what());
        }
        // reload_from_disk() calls schedule_lists_autoupdate() at its end
        return;
    }

    log.info("Lists autoupdate: no relevant changes");
    schedule_lists_autoupdate();
}

void Daemon::update_resolver_config_hash() {
    ListStreamer streamer(cache_);
    const DnsConfig dns_cfg = config_.dns.value_or(DnsConfig{});
    const ResolverType resolver_type = resolver_type_from_dns_config(dns_cfg);
    DnsServerRegistry dns_registry(dns_cfg);
    resolver_config_hash_ = DnsmasqGenerator::compute_config_hash(
        dns_registry,
        streamer,
        config_.route.value_or(RouteConfig{}),
        dns_cfg,
        config_.lists.value_or(std::map<std::string, ListConfig>{}),
        resolver_type);
    Logger::instance().info("Resolver config hash: {}", resolver_config_hash_);
}

void Daemon::update_resolver_config_hash_actual() {
    resolver_config_hash_actual_.clear();

    const auto dns_cfg_opt = config_.dns;
    if (!dns_cfg_opt.has_value() || !dns_cfg_opt->system_resolver.has_value()) {
        return;
    }

    const std::string& resolver_addr = dns_cfg_opt->system_resolver->address;
    if (resolver_addr.empty()) {
        return;
    }

    try {
        std::string error;
        auto txt = query_dns_txt_record(
            resolver_addr, "config-hash.keen.pbr", std::chrono::milliseconds(2000), &error);
        if (!txt.has_value()) {
            if (!error.empty()) {
                Logger::instance().warn("Resolver config hash TXT query failed: {}", error);
            }
            return;
        }

        resolver_config_hash_actual_ = normalize_dns_txt_md5(*txt);
        Logger::instance().info("Resolver config hash (actual): {}",
                                resolver_config_hash_actual_);
    } catch (const std::exception& e) {
        Logger::instance().warn("Resolver config hash TXT query failed: {}", e.what());
    }
}

void Daemon::apply_config(Config config) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);

    // Cancel any pending autoupdate task
    if (lists_autoupdate_task_id_ >= 0) {
        scheduler_->cancel(lists_autoupdate_task_id_);
        lists_autoupdate_task_id_ = -1;
    }

    // Apply new config and fwmarks early so routing is available for downloads
    outbound_marks_ = allocate_outbound_marks(config.fwmark.value_or(FwmarkConfig{}),
                                             config.outbounds.value_or(std::vector<Outbound>{}));
    config_ = std::move(config);
    firewall_state_.set_outbound_marks(outbound_marks_);

    // Install new routing tables/ip rules before downloading so fwmark-based
    // detour routing works and general connectivity is preserved
    setup_static_routing();

    // Download any lists not yet cached (uses per-list detour fwmark if configured)
    download_uncached_lists();

    // Tear down old state — no blocking I/O in this window
    teardown_dns_probe();

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();

    // Re-install routing cleanly after teardown
    setup_static_routing();

    // Re-register urltest outbounds
    register_urltest_outbounds();

    // Rebuild firewall rules
    apply_firewall();

    // Reschedule periodic list autoupdate
    schedule_lists_autoupdate();

    // Recompute resolver config hash after reload
    update_resolver_config_hash();
    update_resolver_config_hash_actual();

    // Recreate DNS test listener with the new config
    setup_dns_probe();

    run_system_resolver_hook_reload();
}


void Daemon::apply_config_with_rollback(const Config& next_config, bool& rolled_back) {
    Config previous_config;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex_);
        previous_config = config_;
    }

    try {
        apply_config(next_config);
        rolled_back = false;
    } catch (...) {
        try {
            apply_config(previous_config);
            rolled_back = true;
        } catch (const std::exception& rollback_error) {
            Logger::instance().error("Rollback to previous config failed: {}", rollback_error.what());
            rolled_back = false;
        } catch (...) {
            Logger::instance().error("Rollback to previous config failed: unknown error");
            rolled_back = false;
        }
        throw;
    }
}

void Daemon::reload_from_disk() {
    std::ifstream ifs(config_path_);
    if (!ifs.is_open()) {
        throw DaemonError("Cannot open config file: " + config_path_);
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    Config next_config = parse_config(ss.str());
    apply_config(std::move(next_config));
}

#ifdef WITH_API
void Daemon::setup_api() {
    if (!config_.api || !config_.api->enabled.value_or(false) || opts_.no_api) return;

    api_server_ = std::make_unique<ApiServer>(*config_.api);
    const std::filesystem::path frontend_root(KEEN_PBR_FRONTEND_ROOT);
    const std::filesystem::path frontend_index = frontend_root / "index.html";
    std::filesystem::path frontend_index_gzip = frontend_index;
    frontend_index_gzip += ".gz";
    const bool has_frontend_root =
        std::filesystem::is_directory(frontend_root) &&
        (std::filesystem::is_regular_file(frontend_index) ||
         std::filesystem::is_regular_file(frontend_index_gzip));
    if (!has_frontend_root) {
        Logger::instance().warn(
            "API enabled but frontend root is unavailable: {} (missing directory or index.html(.gz)). API endpoints will remain available.",
            frontend_root.string());
    } else if (!api_server_->register_static_root(frontend_root.string())) {
        Logger::instance().warn(
            "Failed to register frontend static root: {}. API endpoints will remain available.",
            frontend_root.string());
    } else {
        Logger::instance().info("Frontend static root: {}", frontend_root.string());
    }

    // ApiContext provides synchronized access to Daemon-owned runtime state.
    api_ctx_ = std::make_unique<ApiContext>(ApiContext{
        config_path_,
        cache_,
        state_mutex_,
        *dns_test_broadcaster_,
        [this]() {
            return staged_config_.has_value() ? *staged_config_ : config_;
        },
        [this]() {
            return staged_config_.has_value();
        },
        [this](Config staged_config, std::string staged_config_json) {
            std::unique_lock<std::shared_mutex> lock(state_mutex_);
            staged_config_ = std::move(staged_config);
            staged_config_json_ = std::move(staged_config_json);
        },
        [this]() -> std::optional<std::pair<Config, std::string>> {
            if (!staged_config_.has_value() || !staged_config_json_.has_value()) {
                return std::nullopt;
            }
            return std::make_optional(std::make_pair(*staged_config_, *staged_config_json_));
        },
        [this]() {
            std::unique_lock<std::shared_mutex> lock(state_mutex_);
            staged_config_.reset();
            staged_config_json_.reset();
        },
        [this](const Config& config) {
            const auto marks = allocate_outbound_marks(
                config.fwmark.value_or(FwmarkConfig{}),
                config.outbounds.value_or(std::vector<Outbound>{}));

            std::map<std::string, std::string> urltest_selections;
            {
                std::shared_lock<std::shared_mutex> lock(state_mutex_);
                urltest_selections = firewall_state_.get_urltest_selections();
            }

            (void)build_fw_rule_states(config, marks, &urltest_selections);

            ListStreamer streamer(cache_);
            const DnsConfig dns_cfg = config.dns.value_or(DnsConfig{});
            const ResolverType resolver_type = resolver_type_from_dns_config(dns_cfg);
            DnsServerRegistry dns_registry(dns_cfg);
            (void)DnsmasqGenerator::compute_config_hash(
                dns_registry,
                streamer,
                config.route.value_or(RouteConfig{}),
                dns_cfg,
                config.lists.value_or(std::map<std::string, ListConfig>{}),
                resolver_type);
        },
        [this]() {
            return config_.outbounds.value_or(std::vector<Outbound>{});
        },
        [this](const std::string& tag) -> std::optional<UrltestState> {
            if (!urltest_manager_) return std::nullopt;
            try {
                return urltest_manager_->get_state(tag);
            } catch (const std::out_of_range&) {
                return std::nullopt;
            }
        },
        [this]() {
            return routing_health_checker_->check();
        },
        [this]() {
            return resolver_config_hash_;
        },
        [this]() {
            return resolver_config_hash_actual_;
        },
        config_op_mutex_,
        config_op_cv_,
        config_op_state_,
        [this]() {
            enqueue_control_task([this]() {
                try {
                    reload_from_disk();
                } catch (const std::exception& e) {
                    Logger::instance().error("Reload task failed: {}", e.what());
                }
                {
                    std::lock_guard<std::mutex> op_lock(config_op_mutex_);
                    config_op_state_.store(ConfigOperationState::Idle, std::memory_order_release);
                }
                config_op_cv_.notify_all();
            });
        },
        [this](Config config, std::string saved_config_json) -> ConfigApplyResult {
            ConfigApplyResult result;
            enqueue_control_task([this, &result, config = std::move(config), saved_config_json = std::move(saved_config_json)]() mutable {
                bool rolled_back = false;
                try {
                    apply_config_with_rollback(config, rolled_back);
                    result.applied = true;
                    result.rolled_back = rolled_back;
                    std::unique_lock<std::shared_mutex> lock(state_mutex_);
                    if (staged_config_json_.has_value() && *staged_config_json_ == saved_config_json) {
                        staged_config_.reset();
                        staged_config_json_.reset();
                    }
                } catch (const std::exception& e) {
                    result.error = e.what();
                    result.rolled_back = rolled_back;
                    Logger::instance().error("Apply staged config task failed: {}", e.what());
                }
                {
                    std::lock_guard<std::mutex> op_lock(config_op_mutex_);
                    config_op_state_.store(ConfigOperationState::Idle, std::memory_order_release);
                }
                config_op_cv_.notify_all();
            }, true);
            return result;
        },
    });
    register_api_handlers(*api_server_, *api_ctx_);
    const std::string listen_addr = config_.api->listen.value_or("0.0.0.0:8080");
    Logger::instance().info("Starting REST API on {}", listen_addr);
    try {
        api_server_->start();
        Logger::instance().info("REST API listening on {}", listen_addr);
    } catch (const ApiError& e) {
        Logger::instance().error("REST API startup failed on {}: {}", listen_addr, e.what());
        throw;
    } catch (const std::exception& e) {
        Logger::instance().error("Unexpected REST API startup failure on {}: {}",
                                 listen_addr,
                                 e.what());
        throw;
    }
}
#endif

void Daemon::setup_dns_probe() {
    teardown_dns_probe();

    if (!config_.dns || !config_.dns->dns_test_server.has_value()) {
        return;
    }

    const auto& test_cfg = *config_.dns->dns_test_server;
    const std::string* answer_ip = test_cfg.answer_ipv4 ? &*test_cfg.answer_ipv4 : nullptr;
    auto settings = parse_dns_probe_server_settings(test_cfg.listen, answer_ip);

    auto on_query = [this](const DnsProbeEvent& event) {
#ifdef WITH_API
        if (dns_test_broadcaster_) {
            nlohmann::json payload = {
                {"type", "DNS"},
                {"domain", event.domain},
                {"source_ip", event.source_ip},
                {"ecs", event.ecs.has_value() ? nlohmann::json(*event.ecs) : nlohmann::json(nullptr)},
            };
            dns_test_broadcaster_->publish(payload.dump());
        }
#else
        (void)event;
#endif
    };

    dns_probe_server_ = std::make_unique<DnsProbeServer>(settings, std::move(on_query));

    add_fd(dns_probe_server_->udp_fd(), EPOLLIN, [this](uint32_t events) {
        if ((events & EPOLLIN) && dns_probe_server_) {
            dns_probe_server_->handle_udp_readable();
        }
    });

    add_fd(dns_probe_server_->tcp_fd(), EPOLLIN, [this](uint32_t events) {
        if (!(events & EPOLLIN) || !dns_probe_server_) {
            return;
        }

        for (int client_fd : dns_probe_server_->accept_tcp_clients()) {
            add_fd(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR,
                   [this, client_fd](uint32_t client_events) {
                if (!dns_probe_server_) {
                    remove_fd(client_fd);
                    close(client_fd);
                    return;
                }

                bool keep_alive = false;
                if (client_events & EPOLLIN) {
                    keep_alive = dns_probe_server_->handle_tcp_client_readable(client_fd);
                }
                if (client_events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    keep_alive = false;
                }

                if (!keep_alive) {
                    dns_probe_server_->remove_tcp_client(client_fd);
                    remove_fd(client_fd);
                    close(client_fd);
                }
            });
        }
    });

    Logger::instance().info("DNS test server listening on {}", settings.listen);
}

void Daemon::teardown_dns_probe() {
    if (!dns_probe_server_) {
        return;
    }

    for (int fd : dns_probe_server_->all_fds()) {
        remove_fd(fd);
    }
    dns_probe_server_.reset();
}

} // namespace keen_pbr3
