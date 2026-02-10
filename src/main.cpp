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
#include "health/circuit_breaker.hpp"
#include "health/health_checker.hpp"
#include "lists/list_entry_visitor.hpp"
#include "lists/list_streamer.hpp"
#include "routing/netlink.hpp"
#include "routing/policy_rule.hpp"
#include "routing/route_table.hpp"
#include "routing/target.hpp"

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
        keen_pbr3::HealthChecker health_checker;
        keen_pbr3::CircuitBreaker circuit_breaker;
        auto firewall = keen_pbr3::create_firewall("auto");
        keen_pbr3::NetlinkManager netlink;
        keen_pbr3::RouteTable route_table(netlink);
        keen_pbr3::PolicyRuleManager policy_rules(netlink);

        // Register interface outbounds for health checking
        for (const auto& outbound : config.outbounds) {
            if (auto* iface = std::get_if<keen_pbr3::InterfaceOutbound>(&outbound)) {
                health_checker.register_outbound(*iface);
            }
        }

        // Helper: get tag from any outbound variant
        auto get_outbound_tag = [](const keen_pbr3::Outbound& ob) -> std::string {
            return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
        };

        // Helper: apply routing for a resolved outbound with a given fwmark
        auto apply_routing = [&](const keen_pbr3::Outbound& ob, uint32_t mark) {
            std::visit([&](const auto& outbound) {
                using T = std::decay_t<decltype(outbound)>;
                if constexpr (std::is_same_v<T, keen_pbr3::InterfaceOutbound>) {
                    uint32_t table_id = 100 + (mark & 0xFFFF);
                    keen_pbr3::RuleSpec ip_rule;
                    ip_rule.fwmark = mark;
                    ip_rule.table = table_id;
                    ip_rule.priority = 100 + (mark & 0xFFFF);
                    policy_rules.add(ip_rule);

                    keen_pbr3::RouteSpec route;
                    route.destination = "default";
                    route.table = table_id;
                    route.interface = outbound.interface;
                    if (outbound.gateway.has_value()) {
                        route.gateway = *outbound.gateway;
                    }
                    route_table.add(route);
                } else if constexpr (std::is_same_v<T, keen_pbr3::TableOutbound>) {
                    keen_pbr3::RuleSpec ip_rule;
                    ip_rule.fwmark = mark;
                    ip_rule.table = outbound.table_id;
                    ip_rule.priority = 100 + (mark & 0xFFFF);
                    policy_rules.add(ip_rule);
                } else if constexpr (std::is_same_v<T, keen_pbr3::BlackholeOutbound>) {
                    uint32_t table_id = 100 + (mark & 0xFFFF);
                    keen_pbr3::RuleSpec ip_rule;
                    ip_rule.fwmark = mark;
                    ip_rule.table = table_id;
                    ip_rule.priority = 100 + (mark & 0xFFFF);
                    policy_rules.add(ip_rule);

                    keen_pbr3::RouteSpec route;
                    route.destination = "default";
                    route.table = table_id;
                    route.blackhole = true;
                    route_table.add(route);
                }
            }, ob);
        };

        // Helper: build health check function using circuit breaker state
        auto make_health_fn = [&]() -> keen_pbr3::HealthCheckFn {
            return [&](const std::string& tag) -> bool {
                if (!circuit_breaker.is_allowed(tag)) return false;
                if (!health_checker.has_target(tag)) return true;
                bool healthy = health_checker.check(tag);
                if (healthy) {
                    circuit_breaker.record_success(tag);
                } else {
                    circuit_breaker.record_failure(tag);
                }
                return healthy;
            };
        };

        // Track per-rule resolved outbound tag for SIGUSR1 re-selection
        // Index matches config.route.rules index, stores resolved outbound tag or empty
        std::vector<std::string> rule_outbound_tags;

        // Helper: apply all firewall and routing rules from config + cache
        auto apply_all = [&]() {
            keen_pbr3::ListStreamer list_streamer(cache);
            auto health_fn = make_health_fn();
            rule_outbound_tags.clear();

            uint32_t fwmark = 0x10000;
            for (const auto& rule : config.route.rules) {
                auto decision = keen_pbr3::resolve_route_action(
                    rule.action, config.outbounds, health_fn);

                if (decision.is_skip || !decision.outbound.has_value()) {
                    rule_outbound_tags.emplace_back();
                    ++fwmark;
                    continue;
                }

                const keen_pbr3::Outbound* ob = *decision.outbound;
                if (!ob) {
                    rule_outbound_tags.emplace_back();
                    ++fwmark;
                    continue;
                }

                rule_outbound_tags.push_back(get_outbound_tag(*ob));

                // Create firewall ipsets and stream entries via batch loader
                for (const auto& list_name : rule.lists) {
                    auto list_cfg_it = config.lists.find(list_name);
                    if (list_cfg_it == config.lists.end()) continue;

                    const auto& list_cfg = list_cfg_it->second;

                    // Determine TTL: use list's ttl when the list has domains
                    bool has_domains = !list_cfg.domains.empty() ||
                                       !list_cfg.url.value_or("").empty() ||
                                       list_cfg.file.has_value();
                    uint32_t set_timeout = 0;
                    if (has_domains && list_cfg.ttl > 0) {
                        set_timeout = list_cfg.ttl;
                    }

                    firewall->create_ipset(list_name, AF_INET, set_timeout);

                    // Stream IP/CIDR entries via batch loader
                    int32_t static_timeout = (set_timeout > 0) ? 0 : -1;
                    auto loader = firewall->create_batch_loader(list_name, static_timeout);
                    list_streamer.stream_list(list_name, list_cfg, *loader);
                    loader->finish();

                    // Create mark rule for the ipset
                    firewall->create_mark_rule(list_name, fwmark);
                }

                // Set up routing for this fwmark
                apply_routing(*ob, fwmark);
                ++fwmark;
            }

            firewall->apply();
        };

        // Download lists if not already cached, then apply
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

        apply_all();
        std::cerr << "Firewall rules and routing applied.\n";

        // Set up daemon event loop
        keen_pbr3::Daemon daemon;
        keen_pbr3::Scheduler scheduler(daemon);

        // SIGUSR1: re-evaluate failover route decisions using cached circuit breaker state
        daemon.on_sigusr1([&]() {
            std::cerr << "SIGUSR1: re-evaluating outbound selection...\n";
            auto health_fn = make_health_fn();

            uint32_t fwmark = 0x10000;
            for (size_t i = 0; i < config.route.rules.size(); ++i) {
                const auto& rule = config.route.rules[i];

                auto decision = keen_pbr3::resolve_route_action(
                    rule.action, config.outbounds, health_fn);

                std::string new_tag;
                if (!decision.is_skip && decision.outbound.has_value() && *decision.outbound) {
                    new_tag = get_outbound_tag(**decision.outbound);
                }

                // If outbound changed for this rule, update routes
                if (i < rule_outbound_tags.size() && new_tag != rule_outbound_tags[i]) {
                    std::cerr << "  Rule " << i << ": outbound changed from '"
                              << rule_outbound_tags[i] << "' to '" << new_tag << "'\n";

                    // Remove old routes for this fwmark's table
                    uint32_t table_id = 100 + (fwmark & 0xFFFF);
                    if (!rule_outbound_tags[i].empty()) {
                        keen_pbr3::RouteSpec old_route;
                        old_route.destination = "default";
                        old_route.table = table_id;
                        route_table.remove(old_route);
                    }

                    // Add new routes if there's a new outbound
                    if (!new_tag.empty() && *decision.outbound) {
                        // For route replace: remove old policy rule and route, add new
                        std::visit([&](const auto& outbound) {
                            using T = std::decay_t<decltype(outbound)>;
                            if constexpr (std::is_same_v<T, keen_pbr3::InterfaceOutbound>) {
                                keen_pbr3::RouteSpec route;
                                route.destination = "default";
                                route.table = table_id;
                                route.interface = outbound.interface;
                                if (outbound.gateway.has_value()) {
                                    route.gateway = *outbound.gateway;
                                }
                                route_table.add(route);
                            } else if constexpr (std::is_same_v<T, keen_pbr3::BlackholeOutbound>) {
                                keen_pbr3::RouteSpec route;
                                route.destination = "default";
                                route.table = table_id;
                                route.blackhole = true;
                                route_table.add(route);
                            }
                            // TableOutbound routing is by table_id from policy rule,
                            // which doesn't change per failover
                        }, **decision.outbound);
                    }

                    if (i < rule_outbound_tags.size()) {
                        rule_outbound_tags[i] = new_tag;
                    }
                }

                ++fwmark;
            }
            std::cerr << "SIGUSR1: outbound re-evaluation complete.\n";
        });

        // SIGHUP: full config re-read and rebuild
        daemon.on_sighup([&]() {
            std::cerr << "SIGHUP: full reload starting...\n";
            try {
                // Full teardown
                route_table.clear();
                policy_rules.clear();
                firewall->cleanup();

                // Re-read config
                std::string new_json = read_file(opts.config_path);
                config = keen_pbr3::parse_config(new_json);

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

                // Re-apply everything
                apply_all();
                std::cerr << "SIGHUP: full reload complete.\n";
            } catch (const std::exception& e) {
                std::cerr << "SIGHUP: reload failed: " << e.what() << "\n";
            }
        });

        // Schedule periodic health checks for interface outbounds
        for (const auto& outbound : config.outbounds) {
            if (auto* iface = std::get_if<keen_pbr3::InterfaceOutbound>(&outbound)) {
                if (iface->ping_target.has_value()) {
                    scheduler.schedule_repeating(iface->ping_interval, [&, tag = iface->tag]() {
                        bool healthy = health_checker.check(tag);
                        if (healthy) {
                            circuit_breaker.record_success(tag);
                        } else {
                            circuit_breaker.record_failure(tag);
                        }
                    });
                }
            }
        }

#ifdef WITH_API
        // Optional REST API server
        std::unique_ptr<keen_pbr3::ApiServer> api_server;
        if (config.api.enabled && !opts.no_api) {
            api_server = std::make_unique<keen_pbr3::ApiServer>(config.api);

            keen_pbr3::ApiContext api_ctx{
                config.outbounds,
                cache,
                config.lists,
                health_checker,
                [&]() {
                    // API reload triggers SIGHUP-like behavior
                    route_table.clear();
                    policy_rules.clear();
                    firewall->cleanup();
                    firewall = keen_pbr3::create_firewall("auto");
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
                    apply_all();
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
