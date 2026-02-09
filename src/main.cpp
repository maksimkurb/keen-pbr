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

#include "config/config.hpp"
#include "config/list_parser.hpp"
#include "daemon/daemon.hpp"
#include "daemon/scheduler.hpp"
#include "dns/dns_router.hpp"
#include "dns/dnsmasq_gen.hpp"
#include "firewall/firewall.hpp"
#include "health/circuit_breaker.hpp"
#include "health/health_checker.hpp"
#include "lists/list_manager.hpp"
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

        // Handle print-dnsmasq-config command: load lists, generate, print, exit
        if (opts.print_dnsmasq_config) {
            keen_pbr3::ListManager list_manager(config.lists, config.daemon.cache_dir, true);
            list_manager.load();
            keen_pbr3::DnsRouter dns_router(config.dns, list_manager);
            keen_pbr3::DnsmasqGenerator dnsmasq_gen(dns_router, list_manager,
                                                     config.route, config.dns);
            std::cout << dnsmasq_gen.generate();
            return 0;
        }

        // Daemonize if requested (before creating fds)
        if (opts.daemonize) {
            daemonize_process();
        }

        // Write PID file
        write_pid_file(config.daemon.pid_file);

        // Initialize subsystems
        keen_pbr3::ListManager list_manager(config.lists, config.daemon.cache_dir);
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

        // Download and load all lists
        std::cerr << "Loading lists...\n";
        list_manager.load();

        // Apply firewall rules and routing for loaded lists
        // (IP-based entries go into firewall ipsets; routing rules and policy rules
        //  are set up per route rule with fwmark-based routing)
        uint32_t fwmark = 0x10000; // Starting mark value
        for (const auto& rule : config.route.rules) {
            // Build a health check function that uses the circuit breaker
            keen_pbr3::HealthCheckFn health_fn = [&](const std::string& tag) -> bool {
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

            auto decision = keen_pbr3::resolve_route_action(
                rule.action, config.outbounds, health_fn);

            if (decision.is_skip || !decision.outbound.has_value()) continue;

            const keen_pbr3::Outbound* ob = *decision.outbound;
            if (!ob) continue;

            // Create firewall ipsets and mark rules for each list in this rule
            for (const auto& list_name : rule.lists) {
                const auto* parsed = list_manager.get(list_name);
                if (!parsed) continue;

                std::string set_name = list_name;

                // Determine TTL: use list's ttl when the list has domains
                // (dnsmasq will populate resolved IPs with set-level timeout)
                auto list_cfg_it = config.lists.find(list_name);
                uint32_t set_timeout = 0;
                if (list_cfg_it != config.lists.end() &&
                    !parsed->domains.empty() && list_cfg_it->second.ttl > 0) {
                    set_timeout = list_cfg_it->second.ttl;
                }

                firewall->create_ipset(set_name, AF_INET, set_timeout);

                // Static IPs/CIDRs get timeout 0 (permanent) when set has TTL
                int32_t static_timeout = (set_timeout > 0) ? 0 : -1;
                for (const auto& ip : parsed->ips) {
                    firewall->add_to_ipset(set_name, ip, static_timeout);
                }
                for (const auto& cidr : parsed->cidrs) {
                    firewall->add_to_ipset(set_name, cidr, static_timeout);
                }

                // Create mark rule for the ipset
                firewall->create_mark_rule(set_name, fwmark);
            }

            // Set up routing for this fwmark
            std::visit([&](const auto& outbound) {
                using T = std::decay_t<decltype(outbound)>;
                if constexpr (std::is_same_v<T, keen_pbr3::InterfaceOutbound>) {
                    // Add ip rule: fwmark -> table
                    uint32_t table_id = 100 + (fwmark & 0xFFFF);
                    keen_pbr3::RuleSpec ip_rule;
                    ip_rule.fwmark = fwmark;
                    ip_rule.table = table_id;
                    ip_rule.priority = 100 + (fwmark & 0xFFFF);
                    policy_rules.add(ip_rule);

                    // Add default route in the table
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
                    ip_rule.fwmark = fwmark;
                    ip_rule.table = outbound.table_id;
                    ip_rule.priority = 100 + (fwmark & 0xFFFF);
                    policy_rules.add(ip_rule);
                } else if constexpr (std::is_same_v<T, keen_pbr3::BlackholeOutbound>) {
                    uint32_t table_id = 100 + (fwmark & 0xFFFF);
                    keen_pbr3::RuleSpec ip_rule;
                    ip_rule.fwmark = fwmark;
                    ip_rule.table = table_id;
                    ip_rule.priority = 100 + (fwmark & 0xFFFF);
                    policy_rules.add(ip_rule);

                    keen_pbr3::RouteSpec route;
                    route.destination = "default";
                    route.table = table_id;
                    route.blackhole = true;
                    route_table.add(route);
                }
            }, *ob);

            ++fwmark;
        }

        firewall->apply();
        std::cerr << "Firewall rules and routing applied.\n";

        // Set up daemon event loop
        keen_pbr3::Daemon daemon;
        keen_pbr3::Scheduler scheduler(daemon);

        // Reload callback (used by SIGUSR1 and API)
        auto reload_fn = [&]() {
            std::cerr << "Reloading lists...\n";
            list_manager.reload();
            std::cerr << "Lists reloaded.\n";
        };

        // SIGUSR1 triggers immediate reload
        daemon.on_sigusr1(reload_fn);

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
                list_manager,
                health_checker,
                reload_fn,
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
