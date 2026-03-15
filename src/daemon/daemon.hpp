#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../health/routing_health_checker.hpp"
#include "../health/url_tester.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/netlink.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"

namespace keen_pbr3 {

class Firewall;
class Scheduler;
class UrltestManager;
class DnsProbeServer;

#ifdef WITH_API
class ApiServer;
struct ApiContext;
class SseBroadcaster;
#endif

class DaemonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Callback for file descriptor events
using FdCallback = std::function<void(uint32_t events)>;

// Options controlling daemon runtime behavior
struct DaemonOptions {
    bool no_api{false};
};

// Helper to get tag from any outbound variant
std::string get_outbound_tag(const Outbound& ob);

// Find an outbound by tag, returning pointer or nullptr
const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                               const std::string& tag);

// Epoll-based daemon that owns all runtime subsystems.
// Handles signal dispatch, routing, firewall, urltest, and API lifecycle.
class Daemon {
public:
    Daemon(Config config, std::string config_path, DaemonOptions opts);
    ~Daemon();

    // Non-copyable, non-movable
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    // Register an additional file descriptor for epoll monitoring.
    void add_fd(int fd, uint32_t events, FdCallback cb);

    // Remove a previously registered file descriptor.
    void remove_fd(int fd);

    // Serialize execution of control operations in event loop.
    void enqueue_control_command(std::function<void()> command,
                                 bool wait_for_completion = false);

    // Run the daemon lifecycle: startup, event loop, shutdown.
    void run();

    // Request the event loop to stop.
    void stop();

    // Returns true if the daemon is currently running.
    bool running() const;

private:
    // Epoll/signal setup
    void setup_signals();
    void handle_signal();
    void setup_control_channel();
    void handle_control_commands();
    void wake_control_loop();

    // Signal handlers
    void handle_sigusr1();
    void handle_sighup();

    // Business logic methods
    void setup_static_routing();
    void apply_firewall();
    void download_uncached_lists();
    void register_urltest_outbounds();
    void apply_config(Config config);
    void reload_from_disk();
    void schedule_lists_autoupdate();
    void refresh_lists_and_maybe_reload();

    // PID file management
    void write_pid_file();
    void remove_pid_file();

#ifdef WITH_API
    void setup_api();
#endif
    void setup_dns_probe();
    void teardown_dns_probe();

    // Hash of the current domain-to-ipset mapping (matches dnsmasq txt-record)
    std::string resolver_config_hash_;

    // Recompute resolver_config_hash_ from current config/cache state
    void update_resolver_config_hash();

    // Lists autoupdate state
    int lists_autoupdate_task_id_{-1};

    // Epoll state
    int epoll_fd_{-1};
    int signal_fd_{-1};
    bool running_{false};
    std::thread::id event_loop_thread_id_{};

    struct FdEntry {
        int fd;
        FdCallback callback;
    };
    std::vector<FdEntry> fd_entries_;
    mutable std::mutex fd_entries_mutex_;

    int control_fd_{-1};
    std::vector<std::function<void()>> control_commands_;
    std::mutex control_commands_mutex_;

    // Configuration
    mutable std::shared_mutex state_mutex_;
    Config config_;
    std::string config_path_;
    std::optional<Config> staged_config_;
    std::optional<std::string> staged_config_json_;
    DaemonOptions opts_;

    // Subsystems
    CacheManager cache_;
    std::unique_ptr<Firewall> firewall_;
    NetlinkManager netlink_;
    RouteTable route_table_;
    PolicyRuleManager policy_rules_;
    FirewallState firewall_state_;
    URLTester url_tester_;
    OutboundMarkMap outbound_marks_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<UrltestManager> urltest_manager_;
    std::unique_ptr<RoutingHealthChecker> routing_health_checker_;

#ifdef WITH_API
    std::unique_ptr<ApiServer> api_server_;
    std::unique_ptr<ApiContext> api_ctx_;
    std::unique_ptr<SseBroadcaster> dns_test_broadcaster_;
#endif

    std::unique_ptr<class DnsProbeServer> dns_probe_server_;
};

} // namespace keen_pbr3
