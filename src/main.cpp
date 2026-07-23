#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include <cerrno>
#include <csignal>
#include <unistd.h>

#include <keen-pbr/version.hpp>

#include "config/config.hpp"
#include "crash/crash_diagnostics.hpp"
#include "daemon/daemon.hpp"
#include "http/curl_runtime.hpp"
#include "ipc/control_client.hpp"
#include "ipc/resolver_fallback.hpp"
#include "log/logger.hpp"
#include "util/daemon_signals.hpp"

#ifndef KEEN_PBR_DEFAULT_CONFIG_PATH
#define KEEN_PBR_DEFAULT_CONFIG_PATH "/etc/keen-pbr/config.json"
#endif
#ifndef KEEN_PBR_TARGET_OS
#define KEEN_PBR_TARGET_OS "linux"
#endif
#ifndef KEEN_PBR_TARGET_VERSION
#define KEEN_PBR_TARGET_VERSION "unknown"
#endif
#ifndef KEEN_PBR_TARGET_ARCH
#define KEEN_PBR_TARGET_ARCH "unknown"
#endif
#ifndef KEEN_PBR_BUILD_VARIANT
#define KEEN_PBR_BUILD_VARIANT "full"
#endif
#ifndef KEEN_PBR_GIT_BRANCH
#define KEEN_PBR_GIT_BRANCH "unknown"
#endif
#ifndef KEEN_PBR_GIT_COMMIT
#define KEEN_PBR_GIT_COMMIT "unknown"
#endif
#ifndef KEEN_PBR_RESOLVER_FALLBACK_CONFIG
#define KEEN_PBR_RESOLVER_FALLBACK_CONFIG "/etc/keen-pbr/dnsmasq-fallback.conf"
#endif

namespace {

struct CliOptions {
  std::string config_path{KEEN_PBR_DEFAULT_CONFIG_PATH};
  std::string log_level{"info"};
  std::string pid_file_override;
  std::string crash_report_path{"/tmp/keen-pbr-crash.log"};
  bool no_api{false};
  bool use_raw_prerouting{false};
  bool has_pid_file_override{false};
  bool run_service{false};
  bool generate_resolver_config{false};
  std::string resolver_type;
  bool download_lists{false};
  bool download_reload{false};
  bool resolver_config_hash{false};
  bool run_status{false};
  bool run_test_routing{false};
  std::string test_routing_target;
  bool show_help{false};
  bool show_version{false};
};

void print_usage(const char *argv0) {
  std::cerr << "Usage: " << argv0 << " [options] <command>\n"
            << "\n"
            << "Options:\n"
            << "  --config <path>    Path to JSON config file (default: "
            << KEEN_PBR_DEFAULT_CONFIG_PATH << ")\n"
            << "  --log-level <lvl>  Log level: error, warn, info, verbose, "
               "debug (default: info)\n"
            << "  --pid-file <path>  Override daemon.pid_file when running the "
               "service command\n"
            << "  --crash-report <path>  Last-crash report path (default: "
               "/tmp/keen-pbr-crash.log)\n"
            << "  --no-api           Disable REST API at runtime\n"
            << "  --use-raw-prerouting  Use raw PREROUTING for IPv4 forwarded "
               "traffic (iptables only)\n"
            << "  --version          Show version and exit\n"
            << "  --help             Show this help and exit\n"
            << "\n"
            << "Commands:\n"
            << "  service                            Start the routing service "
               "(foreground)\n"
            << "  status                             Show routing/firewall "
               "status and exit\n"
            << "  download                           Download all configured "
               "lists to cache and exit\n"
            << "  generate-resolver-config <res>     Print generated resolver "
               "config to stdout and exit\n"
            << "                                     Resolvers: dnsmasq "
               "(dnsmasq-ipset and dnsmasq-nftset are deprecated)\n"
            << "  resolver-config-hash               Print MD5 hash of "
               "domain-to-ipset mapping and exit\n"
            << "  test-routing <ip-or-domain>        Test expected vs actual "
               "routing for an IP or domain\n";
}

CliOptions parse_args(int argc, char *argv[]) {
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
    } else if (std::strcmp(argv[i], "--pid-file") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: --pid-file requires an argument\n";
        std::exit(1);
      }
      opts.pid_file_override = argv[++i];
      opts.has_pid_file_override = true;
    } else if (std::strcmp(argv[i], "--crash-report") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: --crash-report requires an argument\n";
        std::exit(1);
      }
      opts.crash_report_path = argv[++i];
    } else if (std::strcmp(argv[i], "--no-api") == 0) {
      opts.no_api = true;
    } else if (std::strcmp(argv[i], "--use-raw-prerouting") == 0) {
      opts.use_raw_prerouting = true;
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      opts.show_help = true;
    } else if (std::strcmp(argv[i], "--version") == 0 ||
               std::strcmp(argv[i], "-v") == 0) {
      opts.show_version = true;
    } else if (std::strcmp(argv[i], "service") == 0) {
      opts.run_service = true;
    } else if (std::strcmp(argv[i], "status") == 0) {
      opts.run_status = true;
    } else if (std::strcmp(argv[i], "generate-resolver-config") == 0) {
      if (i + 1 >= argc) {
        std::cerr
            << "Error: generate-resolver-config requires a resolver argument\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      opts.resolver_type = argv[++i];
      opts.generate_resolver_config = true;
    } else if (std::strcmp(argv[i], "download") == 0) {
      opts.download_lists = true;
    } else if (std::strcmp(argv[i], "--reload") == 0) {
      opts.download_reload = true;
    } else if (std::strcmp(argv[i], "resolver-config-hash") == 0) {
      opts.resolver_config_hash = true;
    } else if (std::strcmp(argv[i], "test-routing") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: test-routing requires an IP address or domain "
                     "argument\n";
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

std::string read_file(const std::string &path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    throw std::runtime_error("Cannot open config file: " + path);
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

void set_signal_action(int signum, void (*handler)(int)) {
  struct sigaction action{};
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  if (sigaction(signum, &action, nullptr) != 0) {
    throw std::runtime_error("sigaction failed: " +
                             std::string(std::strerror(errno)));
  }
}

std::optional<std::string> resolver_fallback_reason(const std::string &error) {
  // Fallback is a lifecycle decision, not a generic IPC error handler.
  // A daemon that is alive (including one in `broken`) remains the source
  // of the managed DNS configuration.  Otherwise an internal error could
  // silently remove all nftset/ipset and domain-routing directives.
  if (error == "runtime_stopped")
    return "runtime_stopped";
  if (error == "runtime_shutting_down")
    return "runtime_shutting_down";
  if (error.find("control socket unavailable") != std::string::npos ||
      error.find("control socket create failed") != std::string::npos) {
    return "daemon_unavailable";
  }
  return std::nullopt;
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  try {
    set_signal_action(SIGPIPE, SIG_IGN);
    sigset_t startup_sigusr1_mask = keen_pbr3::sigusr1_signal_mask();
    keen_pbr3::set_signal_mask_for_current_thread(SIG_BLOCK,
                                                  startup_sigusr1_mask);

    CliOptions opts = parse_args(argc, argv);
    keen_pbr3::crash_diagnostics::CrashReporterConfig crash_config;
    crash_config.report_path = opts.crash_report_path;
    crash_config.version = KEEN_PBR3_VERSION_STRING;
    crash_config.build = KEEN_PBR3_VERSION_RELEASE_STRING;
    crash_config.commit = KEEN_PBR_GIT_COMMIT;
    crash_config.branch = KEEN_PBR_GIT_BRANCH;
    crash_config.target_os = KEEN_PBR_TARGET_OS;
    crash_config.target_version = KEEN_PBR_TARGET_VERSION;
    crash_config.architecture = KEEN_PBR_TARGET_ARCH;
    crash_config.variant = KEEN_PBR_BUILD_VARIANT;
    if (!keen_pbr3::crash_diagnostics::initialize(crash_config)) {
      std::cerr
          << "Error: required crash diagnostics could not be initialized\n";
      return 2;
    }
    keen_pbr3::crash_diagnostics::install_terminate_handler();

    keen_pbr3::CurlRuntime curl_runtime;

    if (!opts.run_service) {
      set_signal_action(SIGUSR1, SIG_IGN);
      keen_pbr3::set_signal_mask_for_current_thread(SIG_UNBLOCK,
                                                    startup_sigusr1_mask);
    }

    if (opts.show_version) {
      std::cout << "keen-pbr " << KEEN_PBR3_VERSION_STRING << " (build "
                << KEEN_PBR3_VERSION_RELEASE << ")" << "\n";
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

    // Initialize logger
    auto &logger = keen_pbr3::Logger::instance();
    logger.set_level(keen_pbr3::parse_log_level(opts.log_level));

    if (opts.generate_resolver_config) {
      if (opts.config_path != KEEN_PBR_DEFAULT_CONFIG_PATH) {
        throw std::runtime_error(
            "--config is only supported with the service command");
      }
      if (opts.resolver_type != "dnsmasq" &&
          opts.resolver_type != "dnsmasq-ipset" &&
          opts.resolver_type != "dnsmasq-nftset") {
        throw std::runtime_error("Unknown resolver type: " +
                                 opts.resolver_type);
      }
      if (opts.resolver_type != "dnsmasq") {
        std::cerr << "Warning: " << opts.resolver_type
                  << " is deprecated; use dnsmasq to select the active daemon "
                     "backend\n";
      }
      try {
        keen_pbr3::ipc::stream_control(
            KEEN_PBR_CONTROL_SOCKET,
            {{"protocol_version", keen_pbr3::ipc::kControlProtocolVersion},
             {"request_id", "cli-generate-resolver-config"},
             {"operation", "generate-resolver-config"},
             {"resolver", opts.resolver_type}},
            std::cout, 15000);
        return 0;
      } catch (const keen_pbr3::ipc::ControlStreamError &error) {
        const auto fallback_reason = resolver_fallback_reason(error.what());
        if (!error.active_bytes_streamed() && fallback_reason.has_value()) {
          if (keen_pbr3::ipc::emit_resolver_fallback(
                  std::cout, KEEN_PBR_RESOLVER_FALLBACK_CONFIG,
                  *fallback_reason,
                  static_cast<std::int64_t>(std::time(nullptr)))) {
            return 0;
          }
        }
        throw;
      }
    }

    if (opts.run_status || opts.resolver_config_hash || opts.download_lists ||
        opts.run_test_routing) {
      if (opts.config_path != KEEN_PBR_DEFAULT_CONFIG_PATH) {
        throw std::runtime_error(
            "--config is only supported with the service command");
      }
      const std::string operation =
          opts.run_status
              ? "status"
              : (opts.resolver_config_hash
                     ? "resolver-config-hash"
                     : (opts.download_lists ? "download" : "test-routing"));
      const auto response = keen_pbr3::ipc::request_control(
          KEEN_PBR_CONTROL_SOCKET,
          {{"protocol_version", keen_pbr3::ipc::kControlProtocolVersion},
           {"request_id", "cli-" + operation},
           {"operation", operation},
           {"reload", opts.download_reload},
           {"target", opts.test_routing_target}});
      if (opts.resolver_config_hash && response.value("ok", false)) {
        std::cout << response.at("result").value("resolver_config_hash", "")
                  << '\n';
      } else {
        std::cout << response.dump() << '\n';
      }
      return response.value("ok", false) ? 0 : 1;
    }

    // Load and parse configuration
    std::string json_str = read_file(opts.config_path);
    keen_pbr3::Config config = keen_pbr3::parse_config(json_str);
    keen_pbr3::validate_config(config);
    if (opts.run_service && opts.has_pid_file_override) {
      if (!config.daemon.has_value()) {
        config.daemon = keen_pbr3::DaemonConfig{};
      }
      config.daemon->pid_file = opts.pid_file_override;
    }

    // Construct Daemon with all subsystems and run
    if (opts.run_service) {
      logger.info("keen-pbr {} starting...", KEEN_PBR3_VERSION_STRING);
      keen_pbr3::DaemonOptions daemon_opts;
      daemon_opts.no_api = opts.no_api;
      daemon_opts.use_raw_prerouting = opts.use_raw_prerouting;

      // Block daemon-managed signals before constructing Daemon so any
      // worker threads spawned during member initialization inherit the mask.
      keen_pbr3::ScopedDaemonSignalMask daemon_signal_mask;
      keen_pbr3::Daemon daemon(std::move(config), opts.config_path,
                               daemon_opts);
      daemon.run();

      logger.info("Shutdown complete.");
    }
    return 0;

  } catch (const keen_pbr3::ConfigValidationError &e) {
    auto &logger = keen_pbr3::Logger::instance();
    logger.error("Configuration validation failed:");
    for (const auto &issue : e.issues()) {
      logger.error("  {}: {}", issue.path, issue.message);
    }
    return 1;
  } catch (const keen_pbr3::ConfigError &e) {
    keen_pbr3::Logger::instance().error("Configuration error: {}", e.what());
    return 1;
  } catch (const std::exception &e) {
    keen_pbr3::Logger::instance().error("Fatal error: {}", e.what());
    return 1;
  }
}
