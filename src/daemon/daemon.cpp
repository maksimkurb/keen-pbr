#include "daemon.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <signal.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../dns/dnsmasq_gen.hpp"
#include "../firewall/firewall.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../routing/target.hpp"
#include "../routing/urltest_manager.hpp"
#include "../config/addr_spec.hpp"
#include "../util/cron.hpp"
#include "scheduler.hpp"

#ifdef WITH_API
#include "../api/handlers.hpp"
#include "../api/server.hpp"
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

Daemon::Daemon(Config config, std::string config_path, DaemonOptions opts)
    : config_(std::move(config))
    , config_path_(std::move(config_path))
    , opts_(std::move(opts))
    , cache_(config_.daemon.value_or(DaemonConfig{}).cache_dir.value_or("/var/cache/keen-pbr3"))
    , firewall_(create_firewall("auto"))
    , netlink_()
    , route_table_(netlink_)
    , policy_rules_(netlink_)
    , firewall_state_()
    , url_tester_()
    , outbound_marks_(allocate_outbound_marks(config_.fwmark.value_or(FwmarkConfig{}),
                                             config_.outbounds.value_or(std::vector<Outbound>{})))
{
    // Initialize epoll
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
    }

    setup_signals();

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
}

Daemon::~Daemon() {
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
        full_reload();
        log.info("SIGHUP: full reload complete.");
    } catch (const std::exception& e) {
        log.error("SIGHUP: reload failed: {}", e.what());
    }
}

void Daemon::add_fd(int fd, uint32_t events, FdCallback cb) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw DaemonError("epoll_ctl add fd failed: " + std::string(strerror(errno)));
    }

    fd_entries_.push_back({fd, std::move(cb)});
}

void Daemon::remove_fd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

    fd_entries_.erase(
        std::remove_if(fd_entries_.begin(), fd_entries_.end(),
                       [fd](const FdEntry& e) { return e.fd == fd; }),
        fd_entries_.end());
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

#ifdef WITH_API
    setup_api();
#endif

    log.info("Daemon running. PID: {}", getpid());

    // --- Event loop ---
    running_ = true;

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

            // Dispatch to registered fd callbacks
            for (auto& entry : fd_entries_) {
                if (entry.fd == fd) {
                    entry.callback(events[i].events);
                    break;
                }
            }
        }
    }

    // --- Shutdown sequence ---
    log.info("Shutting down...");

#ifdef WITH_API
    if (api_server_) {
        api_server_->stop();
    }
#endif

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
    const uint32_t table_start = static_cast<uint32_t>(
        config_.iproute.value_or(IprouteConfig{}).table_start.value_or(100));
    const uint32_t fwmark_mask = static_cast<uint32_t>(
        config_.fwmark.value_or(FwmarkConfig{}).mask.value_or(0x00FF0000));

    uint32_t table_offset = 0;
    for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
        if (ob.type == OutboundType::INTERFACE) {
            auto mark_it = outbound_marks_.find(ob.tag);
            if (mark_it == outbound_marks_.end()) continue;

            uint32_t table_id = table_start + table_offset;
            ++table_offset;

            // Create dedicated routing table with default route via interface
            RouteSpec route;
            route.destination = "default";
            route.table = table_id;
            route.interface = ob.interface.value_or("");
            if (ob.gateway) route.gateway = *ob.gateway;
            route_table_.add(route);

            // Blackhole fallback: fires when interface is removed from kernel,
            // preventing traffic from leaking to the default routing table.
            RouteSpec blackhole_route;
            blackhole_route.destination = "default";
            blackhole_route.table = table_id;
            blackhole_route.blackhole = true;
            blackhole_route.metric = 500;
            route_table_.add(blackhole_route);

            // Add ip rule: fwmark/mask -> table
            RuleSpec ip_rule;
            ip_rule.fwmark = mark_it->second;
            ip_rule.fwmask = fwmark_mask;
            ip_rule.table = table_id;
            ip_rule.priority = table_id;
            policy_rules_.add(ip_rule);
        } else if (ob.type == OutboundType::TABLE) {
            auto mark_it = outbound_marks_.find(ob.tag);
            if (mark_it == outbound_marks_.end()) continue;

            // Add ip rule: fwmark/mask -> table (user-specified table)
            RuleSpec ip_rule;
            ip_rule.fwmark = mark_it->second;
            ip_rule.fwmask = fwmark_mask;
            ip_rule.table = static_cast<uint32_t>(ob.table.value_or(0));
            ip_rule.priority = table_start + table_offset;
            ++table_offset;
            policy_rules_.add(ip_rule);
        } else if (ob.type == OutboundType::URLTEST) {
            auto mark_it = outbound_marks_.find(ob.tag);
            if (mark_it == outbound_marks_.end()) continue;

            uint32_t table_id = table_start + table_offset;
            ++table_offset;

            // Blackhole default: traffic is dropped here when no child is selected,
            // preventing leaks to the default routing table.
            RouteSpec blackhole_route;
            blackhole_route.destination = "default";
            blackhole_route.table = table_id;
            blackhole_route.blackhole = true;
            blackhole_route.metric = 500;
            route_table_.add(blackhole_route);

            // Policy rule: fwmark -> this table
            RuleSpec ip_rule;
            ip_rule.fwmark = mark_it->second;
            ip_rule.fwmask = fwmark_mask;
            ip_rule.table = table_id;
            ip_rule.priority = table_id;
            policy_rules_.add(ip_rule);
        }
        // BLACKHOLE: no routing table, no ip rule — handled by firewall DROP
        // IGNORE: no routing needed
    }
}

void Daemon::apply_firewall() {
    ListStreamer list_streamer(cache_);
    std::vector<RuleState> rule_states;

    // Clean existing firewall state before rebuilding
    firewall_->cleanup();

    const auto& all_outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config_.lists ? *config_.lists : empty_lists;
    const auto& route_rules = config_.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];

        // Resolve the outbound for this rule
        auto decision = resolve_route_action(rule.outbound, all_outbounds);

        if (decision.is_skip) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.list;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        if (!decision.outbound.has_value() || !*decision.outbound) {
            // Unknown outbound tag — skip
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.list;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        const Outbound* ob = *decision.outbound;

        // For urltest outbounds, resolve to the currently selected child
        std::string effective_tag = ob->tag;
        const Outbound* effective_ob = ob;

        if (ob->type == OutboundType::URLTEST) {
            auto selections = firewall_state_.get_urltest_selections();
            auto sel_it = selections.find(effective_tag);
            if (sel_it != selections.end() && !sel_it->second.empty()) {
                const Outbound* child = find_outbound(all_outbounds, sel_it->second);
                if (child) {
                    effective_ob = child;
                    effective_tag = sel_it->second;
                }
            }
        }

        // Determine action based on effective outbound type
        const bool is_blackhole = (effective_ob->type == OutboundType::BLACKHOLE);
        const bool is_ignore    = (effective_ob->type == OutboundType::IGNORE);

        if (is_ignore) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.list;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        RuleState rs;
        rs.rule_index = rule_idx;
        rs.list_names = rule.list;
        rs.outbound_tag = rule.outbound;

        if (is_blackhole) {
            rs.action_type = RuleActionType::Drop;
        } else {
            rs.action_type = RuleActionType::Mark;
            auto mark_it = outbound_marks_.find(effective_tag);
            if (mark_it != outbound_marks_.end()) {
                rs.fwmark = mark_it->second;
            }
        }

        // Create ipsets and stream entries for each list in the rule
        for (const auto& list_name : rule.list) {
            auto list_cfg_it = lists_map.find(list_name);
            if (list_cfg_it == lists_map.end()) continue;

            const auto& list_cfg = list_cfg_it->second;

            // Static sets: permanent IP/CIDR entries (no timeout)
            const std::string set4 = "kpbr4_"  + list_name;
            const std::string set6 = "kpbr6_"  + list_name;
            // Dynamic sets: dnsmasq-resolved entries (TTL from ttl_ms)
            const std::string set4d = "kpbr4d_" + list_name;
            const std::string set6d = "kpbr6d_" + list_name;

            // Compute TTL for the dynamic set (ttl_ms / 1000 → seconds)
            uint32_t dynamic_timeout = 0;
            {
                int64_t ttl_ms = list_cfg.ttl_ms.value_or(0);
                if (ttl_ms >= 1000) {
                    dynamic_timeout = static_cast<uint32_t>(ttl_ms / 1000);
                }
            }

            firewall_->create_ipset(set4,  AF_INET,  0);               // static, permanent
            firewall_->create_ipset(set6,  AF_INET6, 0);               // static, permanent
            firewall_->create_ipset(set4d, AF_INET,  dynamic_timeout); // dynamic, TTL
            firewall_->create_ipset(set6d, AF_INET6, dynamic_timeout); // dynamic, TTL
            rs.set_names.push_back(set4);
            rs.set_names.push_back(set6);
            rs.set_names.push_back(set4d);
            rs.set_names.push_back(set6d);

            // Stream IP/CIDR entries into the static sets (permanent, entry_timeout=-1)
            auto loader4 = firewall_->create_batch_loader(set4, -1);
            auto loader6 = firewall_->create_batch_loader(set6, -1);
            FunctionalVisitor splitter([&](EntryType type, std::string_view entry) {
                if (type == EntryType::Domain) return;
                bool is_v6 = entry.find(':') != std::string_view::npos;
                if (is_v6) loader6->on_entry(type, entry);
                else       loader4->on_entry(type, entry);
            });
            list_streamer.stream_list(list_name, list_cfg, splitter);
            loader4->finish();
            loader6->finish();

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
                firewall_->create_drop_rule(set4,  filter);
                firewall_->create_drop_rule(set6,  filter);
                firewall_->create_drop_rule(set4d, filter);
                firewall_->create_drop_rule(set6d, filter);
            } else if (rs.fwmark != 0) {
                firewall_->create_mark_rule(set4,  rs.fwmark, filter);
                firewall_->create_mark_rule(set6,  rs.fwmark, filter);
                firewall_->create_mark_rule(set4d, rs.fwmark, filter);
                firewall_->create_mark_rule(set6d, rs.fwmark, filter);
            }
        }

        rule_states.push_back(std::move(rs));
    }

    firewall_->apply();
    firewall_state_.set_rules(std::move(rule_states));
}

void Daemon::download_uncached_lists() {
    for (const auto& [name, list_cfg] : config_.lists.value_or(std::map<std::string, ListConfig>{})) {
        if (list_cfg.url.has_value() && !cache_.has_cache(name)) {
            try {
                cache_.download(name, list_cfg.url.value());
            } catch (const std::exception& e) {
                Logger::instance().warn("Failed to download list '{}': {}", name, e.what());
            }
        }
    }
}

void Daemon::register_urltest_outbounds() {
    urltest_manager_ = std::make_unique<UrltestManager>(
        url_tester_, outbound_marks_, *scheduler_,
        [this](const std::string& urltest_tag, const std::string& new_child_tag) {
            auto& log = Logger::instance();
            log.info("Urltest '{}' selected outbound: '{}'", urltest_tag, new_child_tag);
            firewall_state_.set_urltest_selection(urltest_tag, new_child_tag);
            try {
                apply_firewall();
                log.info("Firewall rules rebuilt after urltest change.");
            } catch (const std::exception& e) {
                log.error("Error rebuilding firewall after urltest change: {}", e.what());
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
        try { full_reload(); }
        catch (const std::exception& e) {
            log.error("Lists autoupdate: reload failed: {}", e.what());
        }
        // full_reload() calls schedule_lists_autoupdate() at its end
        return;
    }

    log.info("Lists autoupdate: no relevant changes");
    schedule_lists_autoupdate();
}

void Daemon::update_resolver_config_hash() {
    ListStreamer streamer(cache_);
    resolver_config_hash_ = DnsmasqGenerator::compute_config_hash(
        streamer,
        config_.route.value_or(RouteConfig{}),
        config_.lists.value_or(std::map<std::string, ListConfig>{}));
    Logger::instance().info("Resolver config hash: {}", resolver_config_hash_);
}

void Daemon::full_reload() {
    // Cancel any pending autoupdate task before teardown
    if (lists_autoupdate_task_id_ >= 0) {
        scheduler_->cancel(lists_autoupdate_task_id_);
        lists_autoupdate_task_id_ = -1;
    }

    // Full teardown
    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();

    // Re-read config
    std::ifstream ifs(config_path_);
    if (!ifs.is_open()) {
        throw DaemonError("Cannot open config file: " + config_path_);
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    config_ = parse_config(ss.str());

    // Re-allocate fwmarks
    outbound_marks_ = allocate_outbound_marks(config_.fwmark.value_or(FwmarkConfig{}),
                                             config_.outbounds.value_or(std::vector<Outbound>{}));
    firewall_state_.set_outbound_marks(outbound_marks_);

    // Download any lists not yet cached
    download_uncached_lists();

    // Re-create static routing tables and ip rules
    setup_static_routing();

    // Re-register urltest outbounds
    register_urltest_outbounds();

    // Rebuild firewall rules
    apply_firewall();

    // Reschedule periodic list autoupdate
    schedule_lists_autoupdate();

    // Recompute resolver config hash after reload
    update_resolver_config_hash();
}

#ifdef WITH_API
void Daemon::setup_api() {
    if (!config_.api || !config_.api->enabled.value_or(false) || opts_.no_api) return;

    api_server_ = std::make_unique<ApiServer>(*config_.api);

    // ApiContext holds references to Daemon-owned members (stable addresses).
    // Allocated on heap so it outlives the setup_api() call.
    api_ctx_ = std::make_unique<ApiContext>(ApiContext{
        config_path_,
        config_,
        cache_,
        firewall_state_,
        urltest_manager_,
        *routing_health_checker_,
        resolver_config_hash_,
        [this]() { full_reload(); },
    });
    register_api_handlers(*api_server_, *api_ctx_);
    api_server_->start();
    Logger::instance().info("REST API listening on {}",
                            config_.api->listen.value_or(""));
}
#endif

} // namespace keen_pbr3
