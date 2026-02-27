#include "daemon.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../firewall/firewall.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../routing/target.hpp"
#include "../routing/urltest_manager.hpp"
#include "scheduler.hpp"

#ifdef WITH_API
#include "../api/handlers.hpp"
#include "../api/server.hpp"
#endif

namespace keen_pbr3 {

// Helper to get tag from any outbound variant
std::string get_outbound_tag(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
}

// Find an outbound by tag, returning pointer or nullptr
const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                               const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (get_outbound_tag(ob) == tag) {
            return &ob;
        }
    }
    return nullptr;
}

Daemon::Daemon(Config config, std::string config_path, DaemonOptions opts)
    : config_(std::move(config))
    , config_path_(std::move(config_path))
    , opts_(std::move(opts))
    , cache_(config_.daemon.cache_dir)
    , firewall_(create_firewall("auto"))
    , netlink_()
    , route_table_(netlink_)
    , policy_rules_(netlink_)
    , firewall_state_()
    , url_tester_()
    , outbound_marks_(allocate_outbound_marks(config_.fwmark, config_.outbounds))
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
        for (const auto& ob : config_.outbounds) {
            if (std::holds_alternative<UrltestOutbound>(ob)) {
                const auto& ut = std::get<UrltestOutbound>(ob);
                urltest_manager_->trigger_immediate_test(ut.tag);
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
    const auto& path = config_.daemon.pid_file;
    if (path.empty()) return;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw DaemonError("Cannot write PID file: " + path);
    }
    ofs << getpid() << "\n";
}

void Daemon::remove_pid_file() {
    if (!config_.daemon.pid_file.empty()) {
        std::filesystem::remove(config_.daemon.pid_file);
    }
}

void Daemon::setup_static_routing() {
    uint32_t table_offset = 0;
    for (const auto& ob : config_.outbounds) {
        std::visit([&](const auto& outbound) {
            using T = std::decay_t<decltype(outbound)>;
            if constexpr (std::is_same_v<T, InterfaceOutbound>) {
                auto mark_it = outbound_marks_.find(outbound.tag);
                if (mark_it == outbound_marks_.end()) return;

                uint32_t table_id = config_.iproute.table_start + table_offset;
                ++table_offset;

                // Create dedicated routing table with default route via interface
                RouteSpec route;
                route.destination = "default";
                route.table = table_id;
                route.interface = outbound.interface;
                if (outbound.gateway.has_value()) {
                    route.gateway = *outbound.gateway;
                }
                route_table_.add(route);

                // Add ip rule: fwmark/mask -> table
                RuleSpec ip_rule;
                ip_rule.fwmark = mark_it->second;
                ip_rule.fwmask = config_.fwmark.mask;
                ip_rule.table = table_id;
                ip_rule.priority = table_id;
                policy_rules_.add(ip_rule);
            } else if constexpr (std::is_same_v<T, TableOutbound>) {
                auto mark_it = outbound_marks_.find(outbound.tag);
                if (mark_it == outbound_marks_.end()) return;

                // Add ip rule: fwmark/mask -> table_id (user-specified table)
                RuleSpec ip_rule;
                ip_rule.fwmark = mark_it->second;
                ip_rule.fwmask = config_.fwmark.mask;
                ip_rule.table = outbound.table_id;
                ip_rule.priority = config_.iproute.table_start + table_offset;
                ++table_offset;
                policy_rules_.add(ip_rule);
            }
            // BlackholeOutbound: no routing table, no ip rule — handled by firewall DROP
            // IgnoreOutbound: no routing needed
            // UrltestOutbound: resolved to child at firewall time
        }, ob);
    }
}

void Daemon::apply_firewall() {
    ListStreamer list_streamer(cache_);
    std::vector<RuleState> rule_states;

    // Clean existing firewall state before rebuilding
    firewall_->cleanup();
    firewall_ = create_firewall("auto");

    for (size_t rule_idx = 0; rule_idx < config_.route.rules.size(); ++rule_idx) {
        const auto& rule = config_.route.rules[rule_idx];

        // Resolve the outbound for this rule
        auto decision = resolve_route_action(rule.outbound, config_.outbounds);

        if (decision.is_skip) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.lists;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        if (!decision.outbound.has_value() || !*decision.outbound) {
            // Unknown outbound tag — skip
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.lists;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        const Outbound* ob = *decision.outbound;

        // For urltest outbounds, resolve to the currently selected child
        std::string effective_tag = get_outbound_tag(*ob);
        const Outbound* effective_ob = ob;

        if (std::holds_alternative<UrltestOutbound>(*ob)) {
            auto selections = firewall_state_.get_urltest_selections();
            auto sel_it = selections.find(effective_tag);
            if (sel_it != selections.end() && !sel_it->second.empty()) {
                const Outbound* child =
                    find_outbound(config_.outbounds, sel_it->second);
                if (child) {
                    effective_ob = child;
                    effective_tag = sel_it->second;
                }
            }
        }

        // Determine action based on effective outbound type
        bool is_blackhole = std::holds_alternative<BlackholeOutbound>(*effective_ob);
        bool is_ignore = std::holds_alternative<IgnoreOutbound>(*effective_ob);

        if (is_ignore) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.lists;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        RuleState rs;
        rs.rule_index = rule_idx;
        rs.list_names = rule.lists;
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
        for (const auto& list_name : rule.lists) {
            auto list_cfg_it = config_.lists.find(list_name);
            if (list_cfg_it == config_.lists.end()) continue;

            const auto& list_cfg = list_cfg_it->second;

            // Determine TTL for ipset
            bool has_domains = !list_cfg.domains.empty() ||
                               !list_cfg.url.value_or("").empty() ||
                               list_cfg.file.has_value();
            uint32_t set_timeout = 0;
            if (has_domains && list_cfg.ttl > 0) {
                set_timeout = list_cfg.ttl;
            }

            firewall_->create_ipset(list_name, AF_INET, set_timeout);
            rs.set_names.push_back(list_name);

            // Stream IP/CIDR entries via batch loader
            int32_t static_timeout = (set_timeout > 0) ? 0 : -1;
            auto loader = firewall_->create_batch_loader(list_name, static_timeout);
            list_streamer.stream_list(list_name, list_cfg, *loader);
            loader->finish();

            // Create mark or drop rule for the ipset
            if (is_blackhole) {
                firewall_->create_drop_rule(list_name);
            } else if (rs.fwmark != 0) {
                firewall_->create_mark_rule(list_name, rs.fwmark);
            }
        }

        rule_states.push_back(std::move(rs));
    }

    firewall_->apply();
    firewall_state_.set_rules(std::move(rule_states));
}

void Daemon::download_uncached_lists() {
    for (const auto& [name, list_cfg] : config_.lists) {
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

    for (const auto& ob : config_.outbounds) {
        if (std::holds_alternative<UrltestOutbound>(ob)) {
            const auto& ut = std::get<UrltestOutbound>(ob);
            urltest_manager_->register_urltest(ut);
        }
    }
}

void Daemon::full_reload() {
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
    outbound_marks_ = allocate_outbound_marks(config_.fwmark, config_.outbounds);
    firewall_state_.set_outbound_marks(outbound_marks_);

    // Re-initialize firewall backend
    firewall_ = create_firewall("auto");

    // Download any lists not yet cached
    download_uncached_lists();

    // Re-create static routing tables and ip rules
    setup_static_routing();

    // Re-register urltest outbounds
    register_urltest_outbounds();

    // Rebuild firewall rules
    apply_firewall();
}

#ifdef WITH_API
void Daemon::setup_api() {
    if (!config_.api.enabled || opts_.no_api) return;

    api_server_ = std::make_unique<ApiServer>(config_.api);

    // ApiContext holds references to Daemon-owned members (stable addresses).
    // Allocated on heap so it outlives the setup_api() call.
    api_ctx_ = std::make_unique<ApiContext>(ApiContext{
        config_.outbounds,
        cache_,
        config_.lists,
        firewall_state_,
        *urltest_manager_,
        *routing_health_checker_,
        [this]() { full_reload(); },
    });
    register_api_handlers(*api_server_, *api_ctx_);
    api_server_->start();
    Logger::instance().info("REST API listening on {}", config_.api.listen);
}
#endif

} // namespace keen_pbr3
