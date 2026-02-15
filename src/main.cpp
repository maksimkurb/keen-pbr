#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <keen-pbr3/version.hpp>

#include "cache/cache_manager.hpp"
#include "config/config.hpp"
#include "daemon/daemon.hpp"
#include "dns/dns_router.hpp"
#include "dns/dnsmasq_gen.hpp"
#include "lists/list_entry_visitor.hpp"
#include "lists/list_streamer.hpp"
#include "log/logger.hpp"

namespace {

struct CliOptions {
    std::string config_path{"/etc/keen-pbr3/config.json"};
    std::string log_level{"info"};
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
              << "  --config <path>    Path to JSON config file (default: /etc/keen-pbr3/config.json)\n"
              << "  --log-level <lvl>  Log level: error, warn, info, verbose, debug (default: info)\n"
              << "  -d                 Daemonize (run in background)\n"
              << "  --no-api           Disable REST API at runtime\n"
              << "  --version          Show version and exit\n"
              << "  --help             Show this help and exit\n"
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
        } else if (std::strcmp(argv[i], "--log-level") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --log-level requires an argument\n";
                std::exit(1);
            }
            opts.log_level = argv[++i];
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
        // Initialize logger
        auto& logger = keen_pbr3::Logger::instance();
        logger.set_level(keen_pbr3::parse_log_level(opts.log_level));

        // Load and parse configuration
        logger.info("keen-pbr3 {} starting...", KEEN_PBR3_VERSION_STRING);
        std::string json_str = read_file(opts.config_path);
        keen_pbr3::Config config = keen_pbr3::parse_config(json_str);

        // Handle download command: download all lists to cache, count entries, exit
        if (opts.download_lists) {
            keen_pbr3::CacheManager cache(config.daemon.cache_dir);
            cache.ensure_dir();
            for (const auto& [name, list_cfg] : config.lists) {
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

        // Daemonize if requested (before creating epoll/signalfd)
        if (opts.daemonize) {
            daemonize_process();
        }

        // Construct Daemon with all subsystems and run
        keen_pbr3::DaemonOptions daemon_opts;
        daemon_opts.no_api = opts.no_api;

        keen_pbr3::Daemon daemon(std::move(config), opts.config_path, daemon_opts);
        daemon.run();

        logger.info("Shutdown complete.");
        return 0;

    } catch (const keen_pbr3::ConfigError& e) {
        keen_pbr3::Logger::instance().error("Configuration error: {}", e.what());
        return 1;
    } catch (const std::exception& e) {
        keen_pbr3::Logger::instance().error("Fatal error: {}", e.what());
        return 1;
    }
}
