#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <csignal>
#include <cstdint>
#include <unistd.h>

#if __has_include(<execinfo.h>)
#  include <execinfo.h>
#  define KEEN_HAS_EXECINFO 1
#else
#  include <unwind.h>  // _Unwind_Backtrace — part of libgcc, available on musl
#  define KEEN_HAS_EXECINFO 0
#endif

#include <curl/curl.h>
#include <keen-pbr/version.hpp>

#include "cache/cache_manager.hpp"
#include "config/config.hpp"
#include "cmd/status.hpp"
#include "cmd/test_routing.hpp"
#include "daemon/daemon.hpp"
#include "dns/dns_router.hpp"
#include "dns/dnsmasq_gen.hpp"
#include "lists/list_entry_visitor.hpp"
#include "lists/list_streamer.hpp"
#include "log/logger.hpp"

#ifndef KEEN_PBR_DEFAULT_CONFIG_PATH
#define KEEN_PBR_DEFAULT_CONFIG_PATH "/etc/keen-pbr/config.json"
#endif

namespace {

#if !KEEN_HAS_EXECINFO
// musl / no-execinfo fallback: collect frames via libgcc's unwinder,
// print raw addresses (resolve later with: addr2line -e keen-pbr.debug <addr>)
struct UnwindState { void** frames; int count; int max; };

_Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
    auto* s = static_cast<UnwindState*>(arg);
    if (s->count >= s->max) return _URC_END_OF_STACK;
    s->frames[s->count++] = reinterpret_cast<void*>(_Unwind_GetIP(ctx));
    return _URC_NO_REASON;
}

void write_hex_addr(uintptr_t val) {
    char buf[20] = {'0', 'x'};
    char hex[16]; int hlen = 0;
    do { hex[hlen++] = "0123456789abcdef"[val & 0xf]; val >>= 4; } while (val);
    int len = 2;
    for (int i = hlen - 1; i >= 0; --i) buf[len++] = hex[i];
    buf[len++] = '\n';
    write(STDERR_FILENO, buf, len);
}
#endif // !KEEN_HAS_EXECINFO

void crash_handler(int signum, siginfo_t* /*info*/, void* /*context*/) {
    const char* name = "UNKNOWN";
    switch (signum) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGILL:  name = "SIGILL";  break;
    }

    // write() is async-signal-safe
    const char prefix[] = "\n=== CRASH (signal ";
    write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    write(STDERR_FILENO, name, strlen(name));
    const char suffix[] = ") — stack trace ===\n";
    write(STDERR_FILENO, suffix, sizeof(suffix) - 1);

#if KEEN_HAS_EXECINFO
    void* frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
#else
    void* frames[64];
    UnwindState state{frames, 0, 64};
    _Unwind_Backtrace(unwind_cb, &state);
    const char hint[] = "(raw addresses — resolve with: addr2line -e keen-pbr.debug <addr>)\n";
    write(STDERR_FILENO, hint, sizeof(hint) - 1);
    for (int i = 0; i < state.count; ++i)
        write_hex_addr(reinterpret_cast<uintptr_t>(frames[i]));
#endif

    // Reset to default and re-raise to produce core dump
    signal(signum, SIG_DFL);
    raise(signum);
}

void install_crash_handler() {
    struct sigaction sa{};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
}

struct CliOptions {
    std::string config_path{KEEN_PBR_DEFAULT_CONFIG_PATH};
    std::string log_level{"info"};
    bool no_api{false};
    bool run_service{false};
    bool generate_resolver_config{false};
    std::string resolver_type;
    bool download_lists{false};
    bool resolver_config_hash{false};
    bool run_status{false};
    bool run_test_routing{false};
    std::string test_routing_target;
    bool show_help{false};
    bool show_version{false};
};

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options] <command>\n"
              << "\n"
              << "Options:\n"
              << "  --config <path>    Path to JSON config file (default: " << KEEN_PBR_DEFAULT_CONFIG_PATH << ")\n"
              << "  --log-level <lvl>  Log level: error, warn, info, verbose, debug (default: info)\n"
              << "  --no-api           Disable REST API at runtime\n"
              << "  --version          Show version and exit\n"
              << "  --help             Show this help and exit\n"
              << "\n"
              << "Commands:\n"
              << "  service                            Start the routing service (foreground)\n"
              << "  status                             Show routing/firewall status and exit\n"
              << "  download                           Download all configured lists to cache and exit\n"
              << "  generate-resolver-config <res>     Print generated resolver config to stdout and exit\n"
              << "                                     Resolvers: dnsmasq-ipset, dnsmasq-nftset\n"
              << "  resolver-config-hash               Print MD5 hash of domain-to-ipset mapping and exit\n"
              << "  test-routing <ip-or-domain>        Test expected vs actual routing for an IP or domain\n";
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
        } else if (std::strcmp(argv[i], "--log-level") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --log-level requires an argument\n";
                std::exit(1);
            }
            opts.log_level = argv[++i];
        } else if (std::strcmp(argv[i], "--no-api") == 0) {
            opts.no_api = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            opts.show_help = true;
        } else if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            opts.show_version = true;
        } else if (std::strcmp(argv[i], "service") == 0) {
            opts.run_service = true;
        } else if (std::strcmp(argv[i], "status") == 0) {
            opts.run_status = true;
        } else if (std::strcmp(argv[i], "generate-resolver-config") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: generate-resolver-config requires a resolver argument\n";
                print_usage(argv[0]);
                std::exit(1);
            }
            opts.resolver_type = argv[++i];
            opts.generate_resolver_config = true;
        } else if (std::strcmp(argv[i], "download") == 0) {
            opts.download_lists = true;
        } else if (std::strcmp(argv[i], "resolver-config-hash") == 0) {
            opts.resolver_config_hash = true;
        } else if (std::strcmp(argv[i], "test-routing") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: test-routing requires an IP address or domain argument\n";
                print_usage(argv[0]);
                std::exit(1);
            }
            opts.test_routing_target = argv[++i];
            opts.run_test_routing = true;
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

} // anonymous namespace

int main(int argc, char* argv[]) {
    install_crash_handler();

    struct CurlGuard {
        CurlGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~CurlGuard() { curl_global_cleanup(); }
    };
    CurlGuard curl_guard;

    CliOptions opts = parse_args(argc, argv);

    if (opts.show_version) {
        std::cout << "keen-pbr " << KEEN_PBR3_VERSION_STRING << "\n";
        return 0;
    }

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (!opts.download_lists && !opts.generate_resolver_config &&
        !opts.resolver_config_hash && !opts.run_service && !opts.run_status &&
        !opts.run_test_routing) {
        print_usage(argv[0]);
        return 0;
    }

    try {
        // Initialize logger
        auto& logger = keen_pbr3::Logger::instance();
        logger.set_level(keen_pbr3::parse_log_level(opts.log_level));

        // Load and parse configuration
        std::string json_str = read_file(opts.config_path);
        keen_pbr3::Config config = keen_pbr3::parse_config(json_str);
        keen_pbr3::validate_config(config);

        if (opts.run_status) {
            return keen_pbr3::run_status_command(config, opts.config_path);
        }

        if (opts.run_test_routing) {
            const auto cache_dir = config.daemon.value_or(keen_pbr3::DaemonConfig{})
                                       .cache_dir.value_or("/var/cache/keen-pbr");
            keen_pbr3::CacheManager cache(cache_dir, keen_pbr3::max_file_size_bytes(config));
            return keen_pbr3::run_test_routing_command(config, cache, opts.test_routing_target);
        }

        // Handle download command: download all lists to cache, count entries, exit
        if (opts.download_lists) {
            const auto cache_dir = config.daemon.value_or(keen_pbr3::DaemonConfig{})
                                       .cache_dir.value_or("/var/cache/keen-pbr");
            keen_pbr3::CacheManager cache(cache_dir, keen_pbr3::max_file_size_bytes(config));
            cache.ensure_dir();
            for (const auto& [name, list_cfg] : config.lists.value_or(std::map<std::string, keen_pbr3::ListConfig>{})) {
                if (!list_cfg.url.has_value()) {
                    logger.info("[{}] Skipped (no URL)", name);
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
                        logger.info("[{}] Updated ({} entries)", name, counter.total());
                    } else {
                        logger.info("[{}] Not modified (304)", name);
                    }
                } catch (const std::exception& e) {
                    logger.error("[{}] Error: {}", name, e.what());
                }
            }
            return 0;
        }

        // Handle resolver-config-hash command: print MD5 of full generated directives, exit
        if (opts.resolver_config_hash) {
            const auto cache_dir = config.daemon.value_or(keen_pbr3::DaemonConfig{})
                                       .cache_dir.value_or("/var/cache/keen-pbr");
            keen_pbr3::CacheManager cache(cache_dir, keen_pbr3::max_file_size_bytes(config));
            keen_pbr3::ListStreamer streamer(cache);
            const auto dns_cfg = config.dns.value_or(keen_pbr3::DnsConfig{});
            const auto resolver_type = keen_pbr3::resolver_type_from_dns_config(dns_cfg);
            keen_pbr3::DnsServerRegistry dns_registry(dns_cfg);
            const std::string hash = keen_pbr3::DnsmasqGenerator::compute_config_hash(
                dns_registry,
                streamer,
                config.route.value_or(keen_pbr3::RouteConfig{}),
                dns_cfg,
                config.lists.value_or(std::map<std::string, keen_pbr3::ListConfig>{}),
                resolver_type);
            std::cout << hash << "\n";
            return 0;
        }

        // Handle generate-resolver-config command: load lists, generate, print, exit
        if (opts.generate_resolver_config) {
            const auto cache_dir = config.daemon.value_or(keen_pbr3::DaemonConfig{})
                                       .cache_dir.value_or("/var/cache/keen-pbr");
            keen_pbr3::CacheManager cache(cache_dir, keen_pbr3::max_file_size_bytes(config));
            cache.ensure_dir();
            // Download lists that aren't already cached
            const auto lists_map = config.lists.value_or(std::map<std::string, keen_pbr3::ListConfig>{});
            for (const auto& [name, list_cfg] : lists_map) {
                if (!list_cfg.url.has_value()) {
                    logger.verbose("[{}] Skipped (no URL)", name);
                    continue;
                }
                if (!cache.has_cache(name)) {
                    cache.download(name, list_cfg.url.value());
                    logger.info("[{}] Downloaded", name);
                } else {
                    logger.verbose("[{}] Using cached", name);
                }
            }
            const auto route_cfg = config.route.value_or(keen_pbr3::RouteConfig{});
            const auto dns_cfg   = config.dns.value_or(keen_pbr3::DnsConfig{});
            keen_pbr3::ListStreamer list_streamer(cache);
            keen_pbr3::DnsServerRegistry dns_registry(dns_cfg);
            auto resolver_type = keen_pbr3::DnsmasqGenerator::parse_resolver_type(opts.resolver_type);
            keen_pbr3::DnsmasqGenerator dnsmasq_gen(dns_registry, list_streamer,
                                                     route_cfg, dns_cfg,
                                                     lists_map, resolver_type);
            dnsmasq_gen.generate(std::cout);
            return 0;
        }

        // Construct Daemon with all subsystems and run
        if (opts.run_service) {
            logger.info("keen-pbr {} starting...", KEEN_PBR3_VERSION_STRING);
            keen_pbr3::DaemonOptions daemon_opts;
            daemon_opts.no_api = opts.no_api;

            keen_pbr3::Daemon daemon(std::move(config), opts.config_path, daemon_opts);
            daemon.run();

            logger.info("Shutdown complete.");
        }
        return 0;

    } catch (const keen_pbr3::ConfigValidationError& e) {
        auto& logger = keen_pbr3::Logger::instance();
        logger.error("Configuration validation failed:");
        for (const auto& issue : e.issues()) {
            logger.error("  {}: {}", issue.path, issue.message);
        }
        return 1;
    } catch (const keen_pbr3::ConfigError& e) {
        keen_pbr3::Logger::instance().error("Configuration error: {}", e.what());
        return 1;
    } catch (const std::exception& e) {
        keen_pbr3::Logger::instance().error("Fatal error: {}", e.what());
        return 1;
    }
}
