#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <keen-pbr3/version.hpp>

#include "cache/cache_manager.hpp"
#include "config/config.hpp"
#include "daemon/daemon.hpp"
#include "daemon/scheduler.hpp"
#include "dns/dns_router.hpp"
#include "dns/dnsmasq_gen.hpp"
#include "firewall/firewall.hpp"
#include "health/url_tester.hpp"
#include "lists/list_entry_visitor.hpp"
#include "lists/list_streamer.hpp"
#include "routing/firewall_state.hpp"
#include "routing/netlink.hpp"
#include "routing/policy_rule.hpp"
#include "routing/route_table.hpp"
#include "routing/target.hpp"
#include "routing/urltest_manager.hpp"

#ifdef WITH_API
#include "api/handlers.hpp"
#include "api/server.hpp"
#endif

namespace {

struct CliOptions {
    std::string config_path{"/etc/keen-pbr3/config.json"};
    bool daemonize{false};
    bool no_api{false};
    bool print_dnsmasq_config{false};
    bool download_lists{false};
    bool show_help{false};
    bool show_version{false};
};

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --config <path>  Path to JSON config file (default: /etc/keen-pbr3/config.json)\n"
              << "  -d               Daemonize (run in background)\n"
              << "  --no-api         Disable REST API at runtime\n"
              << "  --version        Show version and exit\n"
              << "  --help           Show this help and exit\n"
              << "\n"
              << "Commands:\n"
              << "  download              Download all configured lists to cache and exit\n"
              << "  print-dnsmasq-config  Print generated dnsmasq config to stdout and exit\n";
}

CliOptions parse_args(int argc, char* argv[]) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --config requires an argument\n";
                std::exit(1);
            }
            opts.config_path = argv[++i];
        } else if (std::strcmp(argv[i], "-d") == 0) {
            opts.daemonize = true;
        } else if (std::strcmp(argv[i], "--no-api") == 0) {
            opts.no_api = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            opts.show_help = true;
        } else if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            opts.show_version = true;
        } else if (std::strcmp(argv[i], "print-dnsmasq-config") == 0) {
            opts.print_dnsmasq_config = true;
        } else if (std::strcmp(argv[i], "download") == 0) {
            opts.download_lists = true;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }
    return opts;
}

std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

void write_pid_file(const std::string& path) {
    if (path.empty()) return;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot write PID file: " + path);
    }
    ofs << getpid() << "\n";
}

void remove_pid_file(const std::string& path) {
    if (!path.empty()) {
        std::filesystem::remove(path);
    }
}

void daemonize_process() {
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("Failed to fork: " + std::string(std::strerror(errno)));
    }
    if (pid > 0) {
        // Parent exits
        std::exit(0);
    }
    // Child becomes session leader
    if (setsid() < 0) {
        throw std::runtime_error("Failed to setsid: " + std::string(std::strerror(errno)));
    }
    // Redirect stdin/stdout/stderr to /dev/null
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

// Helper to get tag from any outbound variant
std::string get_outbound_tag(const keen_pbr3::Outbound& ob) {
    return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
}

// Find an outbound by tag, returning pointer or nullptr
const keen_pbr3::Outbound* find_outbound(const std::vector<keen_pbr3::Outbound>& outbounds,
                                          const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (get_outbound_tag(ob) == tag) {
            return &ob;
        }
    }
    return nullptr;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    CliOptions opts = parse_args(argc, argv);

    if (opts.show_version) {
        std::cout << "keen-pbr3 " << KEEN_PBR3_VERSION_STRING << "\n";
        return 0;
    }

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    try {
        // Load and parse configuration
        std::cerr << "keen-pbr3 " << KEEN_PBR3_VERSION_STRING << " starting...\n";
        std::string json_str = read_file(opts.config_path);
        keen_pbr3::Config config = keen_pbr3::parse_config(json_str);

        // Handle download command: download all lists to cache, count entries, exit
        if (opts.download_lists) {
            keen_pbr3::CacheManager cache(config.daemon.cache_dir);
            cache.ensure_dir();
            for (const auto& [name, list_cfg] : config.lists) {
                if (!list_cfg.url.has_value()) {
                    std::cerr << "[" << name << "] Skipped (no URL)\n";
                    continue;
                }
                try {
                    bool updated = cache.download(name, list_cfg.url.value());
                    if (updated) {
                        // Count entries by streaming through EntryCounter
                        keen_pbr3::ListStreamer streamer(cache);
                        keen_pbr3::EntryCounter counter;
                        streamer.stream_list(name, list_cfg, counter);
                        // Update metadata with counts
                        auto meta = cache.load_metadata(name);
                        meta.ips = counter.ips();
                        meta.cidrs = counter.cidrs();
                        meta.domains = counter.domains();
                        cache.save_metadata(name, meta);
                        std::cerr << "[" << name << "] Updated ("
                                  << counter.total() << " entries)\n";
                    } else {
                        std::cerr << "[" << name << "] Not modified (304)\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[" << name << "] Error: " << e.what() << "\n";
                }
            }
            return 0;
        }

        // Handle print-dnsmasq-config command: load lists, generate, print, exit
        if (opts.print_dnsmasq_config) {
            keen_pbr3::CacheManager cache(config.daemon.cache_dir);
            cache.ensure_dir();
            // Download lists that aren't already cached
            for (const auto& [name, list_cfg] : config.lists) {
                if (list_cfg.url.has_value() && !cache.has_cache(name)) {
                    cache.download(name, list_cfg.url.value());
                }
            }
            keen_pbr3::ListStreamer list_streamer(cache);
            keen_pbr3::DnsServerRegistry dns_registry(config.dns);
            keen_pbr3::DnsmasqGenerator dnsmasq_gen(dns_registry, list_streamer,
                                                     config.route, config.dns,
                                                     config.lists);
            dnsmasq_gen.generate(std::cout);
            return 0;
        }

        // Daemonize if requested (before creating fds)
        if (opts.daemonize) {
            daemonize_process();
        }

        // Write PID file
        write_pid_file(config.daemon.pid_file);

        // Initialize subsystems
        keen_pbr3::CacheManager cache(config.daemon.cache_dir);
        cache.ensure_dir();
        auto firewall = keen_pbr3::create_firewall("auto");
        keen_pbr3::NetlinkManager netlink;
        keen_pbr3::RouteTable route_table(netlink);
        keen_pbr3::PolicyRuleManager policy_rules(netlink);
        keen_pbr3::FirewallState firewall_state;
        keen_pbr3::URLTester url_tester;

        // Allocate fwmarks to routable outbounds (interface, table)
        keen_pbr3::OutboundMarkMap outbound_marks =
            keen_pbr3::allocate_outbound_marks(config.fwmark, config.outbounds);
        firewall_state.set_outbound_marks(outbound_marks);

        // Set up static routing tables and ip rules for each routable outbound.
        // These are created once at startup and never modified during runtime.
        auto setup_static_routing = [&]() {
            uint32_t table_offset = 0;
            for (const auto& ob : config.outbounds) {
                std::visit([&](const auto& outbound) {
                    using T = std::decay_t<decltype(outbound)>;
                    if constexpr (std::is_same_v<T, keen_pbr3::InterfaceOutbound>) {
                        auto mark_it = outbound_marks.find(outbound.tag);
                        if (mark_it == outbound_marks.end()) return;

                        uint32_t table_id = config.iproute.table_start + table_offset;
                        ++table_offset;

                        // Create dedicated routing table with default route via interface
                        keen_pbr3::RouteSpec route;
                        route.destination = "default";
                        route.table = table_id;
                        route.interface = outbound.interface;
                        if (outbound.gateway.has_value()) {
                            route.gateway = *outbound.gateway;
                        }
                        route_table.add(route);

                        // Add ip rule: fwmark/mask -> table
                        keen_pbr3::RuleSpec ip_rule;
                        ip_rule.fwmark = mark_it->second;
                        ip_rule.fwmask = config.fwmark.mask;
                        ip_rule.table = table_id;
                        ip_rule.priority = table_id;
                        policy_rules.add(ip_rule);
                    } else if constexpr (std::is_same_v<T, keen_pbr3::TableOutbound>) {
                        auto mark_it = outbound_marks.find(outbound.tag);
                        if (mark_it == outbound_marks.end()) return;

                        // Add ip rule: fwmark/mask -> table_id (user-specified table)
                        keen_pbr3::RuleSpec ip_rule;
                        ip_rule.fwmark = mark_it->second;
                        ip_rule.fwmask = config.fwmark.mask;
                        ip_rule.table = outbound.table_id;
                        ip_rule.priority = config.iproute.table_start + table_offset;
                        ++table_offset;
                        policy_rules.add(ip_rule);
                    }
                    // BlackholeOutbound: no routing table, no ip rule — handled by firewall DROP
                    // IgnoreOutbound: no routing needed
                    // UrltestOutbound: resolved to child at firewall time
                }, ob);
            }
        };

        // apply_firewall(): builds complete firewall ruleset transactionally, updates FirewallState.
        // Called on startup, on urltest selection change, and on SIGHUP.
        auto apply_firewall = [&]() {
            keen_pbr3::ListStreamer list_streamer(cache);
            std::vector<keen_pbr3::RuleState> rule_states;

            // Clean existing firewall state before rebuilding
            firewall->cleanup();
            firewall = keen_pbr3::create_firewall("auto");

            for (size_t rule_idx = 0; rule_idx < config.route.rules.size(); ++rule_idx) {
                const auto& rule = config.route.rules[rule_idx];

                // Resolve the outbound for this rule
                auto decision = keen_pbr3::resolve_route_action(
                    rule.outbound, config.outbounds);

                if (decision.is_skip) {
                    // IgnoreOutbound: no ipset, no rule
                    keen_pbr3::RuleState rs;
                    rs.rule_index = rule_idx;
                    rs.list_names = rule.lists;
                    rs.outbound_tag = rule.outbound;
                    rs.action_type = keen_pbr3::RuleActionType::Skip;
                    rule_states.push_back(std::move(rs));
                    continue;
                }

                if (!decision.outbound.has_value() || !*decision.outbound) {
                    // Unknown outbound tag — skip
                    keen_pbr3::RuleState rs;
                    rs.rule_index = rule_idx;
                    rs.list_names = rule.lists;
                    rs.outbound_tag = rule.outbound;
                    rs.action_type = keen_pbr3::RuleActionType::Skip;
                    rule_states.push_back(std::move(rs));
                    continue;
                }

                const keen_pbr3::Outbound* ob = *decision.outbound;

                // For urltest outbounds, resolve to the currently selected child
                std::string effective_tag = get_outbound_tag(*ob);
                const keen_pbr3::Outbound* effective_ob = ob;

                if (std::holds_alternative<keen_pbr3::UrltestOutbound>(*ob)) {
                    // Look up the currently selected child from FirewallState
                    auto selections = firewall_state.get_urltest_selections();
                    auto sel_it = selections.find(effective_tag);
                    if (sel_it != selections.end() && !sel_it->second.empty()) {
                        const keen_pbr3::Outbound* child =
                            find_outbound(config.outbounds, sel_it->second);
                        if (child) {
                            effective_ob = child;
                            effective_tag = sel_it->second;
                        }
                    }
                }

                // Determine action based on effective outbound type
                bool is_blackhole = std::holds_alternative<keen_pbr3::BlackholeOutbound>(*effective_ob);
                bool is_ignore = std::holds_alternative<keen_pbr3::IgnoreOutbound>(*effective_ob);

                if (is_ignore) {
                    keen_pbr3::RuleState rs;
                    rs.rule_index = rule_idx;
                    rs.list_names = rule.lists;
                    rs.outbound_tag = rule.outbound;
                    rs.action_type = keen_pbr3::RuleActionType::Skip;
                    rule_states.push_back(std::move(rs));
                    continue;
                }

                keen_pbr3::RuleState rs;
                rs.rule_index = rule_idx;
                rs.list_names = rule.lists;
                rs.outbound_tag = rule.outbound;

                if (is_blackhole) {
                    rs.action_type = keen_pbr3::RuleActionType::Drop;
                } else {
                    rs.action_type = keen_pbr3::RuleActionType::Mark;
                    auto mark_it = outbound_marks.find(effective_tag);
                    if (mark_it != outbound_marks.end()) {
                        rs.fwmark = mark_it->second;
                    }
                }

                // Create ipsets and stream entries for each list in the rule
                for (const auto& list_name : rule.lists) {
                    auto list_cfg_it = config.lists.find(list_name);
                    if (list_cfg_it == config.lists.end()) continue;

                    const auto& list_cfg = list_cfg_it->second;

                    // Determine TTL for ipset
                    bool has_domains = !list_cfg.domains.empty() ||
                                       !list_cfg.url.value_or("").empty() ||
                                       list_cfg.file.has_value();
                    uint32_t set_timeout = 0;
                    if (has_domains && list_cfg.ttl > 0) {
                        set_timeout = list_cfg.ttl;
                    }

                    firewall->create_ipset(list_name, AF_INET, set_timeout);
                    rs.set_names.push_back(list_name);

                    // Stream IP/CIDR entries via batch loader
                    int32_t static_timeout = (set_timeout > 0) ? 0 : -1;
                    auto loader = firewall->create_batch_loader(list_name, static_timeout);
                    list_streamer.stream_list(list_name, list_cfg, *loader);
                    loader->finish();

                    // Create mark or drop rule for the ipset
                    if (is_blackhole) {
                        firewall->create_drop_rule(list_name);
                    } else if (rs.fwmark != 0) {
                        firewall->create_mark_rule(list_name, rs.fwmark);
                    }
                }

                rule_states.push_back(std::move(rs));
            }

            firewall->apply();
            firewall_state.set_rules(std::move(rule_states));
        };

        // Download lists if not already cached
        std::cerr << "Loading lists...\n";
        for (const auto& [name, list_cfg] : config.lists) {
            if (list_cfg.url.has_value() && !cache.has_cache(name)) {
                try {
                    cache.download(name, list_cfg.url.value());
                } catch (const std::exception& e) {
                    std::cerr << "Warning: failed to download list '" << name
                              << "': " << e.what() << "\n";
                }
            }
        }

        // Set up static routing tables and ip rules
        setup_static_routing();
        std::cerr << "Static routing tables and ip rules installed.\n";

        // Set up daemon event loop
        keen_pbr3::Daemon daemon;
        keen_pbr3::Scheduler scheduler(daemon);

        // Initialize UrltestManager with change callback that triggers firewall rebuild
        keen_pbr3::UrltestManager urltest_manager(
            url_tester, outbound_marks, scheduler,
            [&](const std::string& urltest_tag, const std::string& new_child_tag) {
                std::cerr << "Urltest '" << urltest_tag << "' selected outbound: '"
                          << new_child_tag << "'\n";
                firewall_state.set_urltest_selection(urltest_tag, new_child_tag);
                try {
                    apply_firewall();
                    std::cerr << "Firewall rules rebuilt after urltest change.\n";
                } catch (const std::exception& e) {
                    std::cerr << "Error rebuilding firewall after urltest change: "
                              << e.what() << "\n";
                }
            });

        // Register urltest outbounds
        for (const auto& ob : config.outbounds) {
            if (std::holds_alternative<keen_pbr3::UrltestOutbound>(ob)) {
                const auto& ut = std::get<keen_pbr3::UrltestOutbound>(ob);
                urltest_manager.register_urltest(ut);
            }
        }

        // Apply initial firewall rules
        apply_firewall();
        std::cerr << "Firewall rules and routing applied.\n";

        // SIGUSR1: verify static routing tables/ip rules + trigger immediate URL tests
        daemon.on_sigusr1([&]() {
            std::cerr << "SIGUSR1: verifying routing tables and triggering URL tests...\n";

            // Re-add static routing tables/ip rules in case they were lost
            try {
                route_table.clear();
                policy_rules.clear();
                setup_static_routing();
                std::cerr << "SIGUSR1: static routing tables verified.\n";
            } catch (const std::exception& e) {
                std::cerr << "SIGUSR1: error verifying routing: " << e.what() << "\n";
            }

            // Trigger immediate URL tests for all urltest outbounds
            for (const auto& ob : config.outbounds) {
                if (std::holds_alternative<keen_pbr3::UrltestOutbound>(ob)) {
                    const auto& ut = std::get<keen_pbr3::UrltestOutbound>(ob);
                    urltest_manager.trigger_immediate_test(ut.tag);
                }
            }

            std::cerr << "SIGUSR1: complete.\n";
        });

        // SIGHUP: full config re-read and rebuild
        daemon.on_sighup([&]() {
            std::cerr << "SIGHUP: full reload starting...\n";
            try {
                // Full teardown
                urltest_manager.clear();
                route_table.clear();
                policy_rules.clear();
                firewall->cleanup();

                // Re-read config
                std::string new_json = read_file(opts.config_path);
                config = keen_pbr3::parse_config(new_json);

                // Re-allocate fwmarks
                outbound_marks = keen_pbr3::allocate_outbound_marks(
                    config.fwmark, config.outbounds);
                firewall_state.set_outbound_marks(outbound_marks);

                // Re-initialize firewall backend
                firewall = keen_pbr3::create_firewall("auto");

                // Download any lists not yet cached
                for (const auto& [name, list_cfg] : config.lists) {
                    if (list_cfg.url.has_value() && !cache.has_cache(name)) {
                        try {
                            cache.download(name, list_cfg.url.value());
                        } catch (const std::exception& e) {
                            std::cerr << "Warning: failed to download list '" << name
                                      << "': " << e.what() << "\n";
                        }
                    }
                }

                // Re-create static routing tables and ip rules
                setup_static_routing();

                // Re-register urltest outbounds
                for (const auto& ob : config.outbounds) {
                    if (std::holds_alternative<keen_pbr3::UrltestOutbound>(ob)) {
                        const auto& ut = std::get<keen_pbr3::UrltestOutbound>(ob);
                        urltest_manager.register_urltest(ut);
                    }
                }

                // Rebuild firewall rules
                apply_firewall();
                std::cerr << "SIGHUP: full reload complete.\n";
            } catch (const std::exception& e) {
                std::cerr << "SIGHUP: reload failed: " << e.what() << "\n";
            }
        });

#ifdef WITH_API
        // Optional REST API server
        std::unique_ptr<keen_pbr3::ApiServer> api_server;
        if (config.api.enabled && !opts.no_api) {
            api_server = std::make_unique<keen_pbr3::ApiServer>(config.api);

            keen_pbr3::ApiContext api_ctx{
                config.outbounds,
                cache,
                config.lists,
                [&]() {
                    // API reload triggers SIGHUP-like behavior
                    urltest_manager.clear();
                    route_table.clear();
                    policy_rules.clear();
                    firewall->cleanup();
                    firewall = keen_pbr3::create_firewall("auto");
                    outbound_marks = keen_pbr3::allocate_outbound_marks(
                        config.fwmark, config.outbounds);
                    firewall_state.set_outbound_marks(outbound_marks);
                    for (const auto& [name, list_cfg] : config.lists) {
                        if (list_cfg.url.has_value() && !cache.has_cache(name)) {
                            try {
                                cache.download(name, list_cfg.url.value());
                            } catch (const std::exception& e) {
                                std::cerr << "Warning: failed to download list '" << name
                                          << "': " << e.what() << "\n";
                            }
                        }
                    }
                    setup_static_routing();
                    for (const auto& ob : config.outbounds) {
                        if (std::holds_alternative<keen_pbr3::UrltestOutbound>(ob)) {
                            const auto& ut = std::get<keen_pbr3::UrltestOutbound>(ob);
                            urltest_manager.register_urltest(ut);
                        }
                    }
                    apply_firewall();
                },
            };
            keen_pbr3::register_api_handlers(*api_server, api_ctx);
            api_server->start();
            std::cerr << "REST API listening on " << config.api.listen << "\n";
        }
#endif

        std::cerr << "Daemon running. PID: " << getpid() << "\n";
        daemon.run();

        // Graceful shutdown
        std::cerr << "Shutting down...\n";

#ifdef WITH_API
        if (api_server) {
            api_server->stop();
        }
#endif

        urltest_manager.clear();
        scheduler.cancel_all();
        route_table.clear();
        policy_rules.clear();
        firewall->cleanup();
        remove_pid_file(config.daemon.pid_file);

        std::cerr << "Shutdown complete.\n";
        return 0;

    } catch (const keen_pbr3::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
