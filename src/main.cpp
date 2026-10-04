#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <termios.h>

#include <keen-pbr/version.hpp>

#include "config/config.hpp"
#include "config/config_writer.hpp"
#include "cache/cache_manager.hpp"
#include "auth/password.hpp"
#include "cmd/status.hpp"
#include "cmd/test_routing.hpp"
#include "dns/dnsmasq_gen.hpp"
#include "crash/crash_diagnostics.hpp"
#include "daemon/daemon.hpp"
#include "dns/dns_router.hpp"
#include "http/curl_runtime.hpp"
#include "ipc/control_client.hpp"
#include "log/logger.hpp"
#include "lists/list_streamer.hpp"
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

namespace {

struct CliOptions {
  std::string config_path{KEEN_PBR_DEFAULT_CONFIG_PATH};
  std::string log_level{"info"};
  std::string log_target;
  std::string pid_file_override;
  std::string crash_report_path{"/tmp/keen-pbr-crash.log"};
  bool no_api{false};
  bool use_raw_prerouting{false};
  bool use_raw6_prerouting{false};
  bool has_pid_file_override{false};
  bool run_service{false};
  bool generate_resolver_config{false};
  std::string resolver_type;
  bool download_lists{false};
  bool download_reload{false};
  bool resolver_config_hash{false};
  bool run_status{false};
  bool run_test_routing{false};
  bool run_dnsmasq_config{false};
  std::string test_routing_target;
  bool show_help{false};
  bool show_version{false};
  bool hash_password{false};
  bool update_password{false};
};

void print_usage(const char *argv0) {
  std::cerr << "Usage: " << argv0 << " [options] <command>\n"
            << "\n"
            << "Options:\n"
            << "  --config <path>    Path to JSON config file (default: "
            << KEEN_PBR_DEFAULT_CONFIG_PATH << ")\n"
            << "  --log-level <lvl>  Log level: error, warn, info, verbose, "
               "debug (default: info)\n"
            << "  --log-target <target>  Log destination: stderr, syslog, or "
               "both (default: syslog for service, stderr otherwise)\n"
            << "  --pid-file <path>  Override daemon.pid_file when running the "
               "service command\n"
            << "  --crash-report <path>  Last-crash report path (default: "
               "/tmp/keen-pbr-crash.log)\n"
            << "  --no-api           Disable REST API at runtime\n"
            << "  --use-raw-prerouting  Use raw PREROUTING for IPv4 forwarded "
               "traffic (iptables only)\n"
            << "  --use-raw6-prerouting Use raw PREROUTING for IPv6 forwarded "
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
            << "  test-routing <ip-or-domain>        Test expected vs actual "
               "routing for an IP or domain\n"
            << "  generate-resolver-config dnsmasq   Print the dnsmasq config generated "
               "from dns.rules/dns.fallback (cached lists only; used as "
               "dnsmasq conf-script, always exits 0)\n"
            << "  dnsmasq-config                     Alias of generate-resolver-config dnsmasq\n"
            << "  hash-password [--update]           Generate an authentication password hash; --update writes config.json\n";
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
    } else if (std::strcmp(argv[i], "--log-target") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: --log-target requires an argument\n";
        std::exit(1);
      }
      opts.log_target = argv[++i];
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
    } else if (std::strcmp(argv[i], "--use-raw6-prerouting") == 0) {
      opts.use_raw6_prerouting = true;
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
      // A missing argument must not fail (dnsmasq conf-script): it is
      // reported as a comment line later.
      if (i + 1 < argc) opts.resolver_type = argv[++i];
      opts.generate_resolver_config = true;
      opts.run_dnsmasq_config = true;
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
    } else if (std::strcmp(argv[i], "dnsmasq-config") == 0) {
      // Alias of `generate-resolver-config dnsmasq`.
      opts.run_dnsmasq_config = true;
      opts.resolver_type = "dnsmasq";
    } else if (std::strcmp(argv[i], "hash-password") == 0) {
      opts.hash_password = true;
    } else if (std::strcmp(argv[i], "--update") == 0) {
      opts.update_password = true;
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

std::string read_secret(const char *prompt) {
  std::cerr << prompt << std::flush;
  termios old_settings{};
  const bool tty = ::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &old_settings) == 0;
  if (tty) {
    auto hidden = old_settings;
    hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
  }
  std::string value;
  std::getline(std::cin, value);
  if (tty) {
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_settings);
    std::cerr << '\n';
  }
  return value;
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

nlohmann::json request_control_state(const std::string &operation,
                                     const std::string &target = "",
                                     nlohmann::json fields = nlohmann::json::object()) {
  constexpr std::chrono::milliseconds backoff[] = {
      std::chrono::milliseconds{0}, std::chrono::milliseconds{100},
      std::chrono::milliseconds{300}, std::chrono::milliseconds{700}};
  std::exception_ptr last_error;
  for (const auto delay : backoff) {
    if (delay.count() != 0)
      std::this_thread::sleep_for(delay);
    try {
      fields["protocol_version"] = keen_pbr3::ipc::kControlProtocolVersion;
      fields["request_id"] =
          "cli-" + operation + "-" + std::to_string(getpid());
      fields["operation"] = operation;
      if (!target.empty()) fields["target"] = target;
      auto response = keen_pbr3::ipc::request_control(
          KEEN_PBR_CONTROL_SOCKET,
          fields,
          1000, 5000);
      const auto code = response.value("error", nlohmann::json::object())
                            .value("code", "");
      if (code != "busy")
        return response;
    } catch (const keen_pbr3::ipc::ControlProtocolError &) {
      last_error = std::current_exception();
    }
  }
  if (last_error)
    std::rethrow_exception(last_error);
  throw keen_pbr3::ipc::ControlTimeoutError(
      "control socket remained busy after retries");
}

std::vector<keen_pbr3::RuleState>
parse_realized_rules(const nlohmann::json &result) {
  std::vector<keen_pbr3::RuleState> rules;
  for (const auto &item :
       result.value("realized_rules", nlohmann::json::array())) {
    const int action = item.value("action_type", 3);
    if (action < static_cast<int>(keen_pbr3::RuleActionType::Mark) ||
        action > static_cast<int>(keen_pbr3::RuleActionType::Skip)) {
      throw std::runtime_error("Invalid realized control rule action");
    }
    keen_pbr3::RuleState rule{};
    rule.rule_index = item.value("rule_index", std::size_t{0});
    rule.set_names = item.value("set_names", std::vector<std::string>{});
    rule.outbound_tag = item.value("outbound_tag", "");
    rule.action_type = static_cast<keen_pbr3::RuleActionType>(action);
    rule.fwmark = item.value("fwmark", std::uint32_t{0});
    rules.push_back(std::move(rule));
  }
  return rules;
}

keen_pbr3::Config load_committed_config(const std::string &path) {
  std::ifstream input(path);
  if (!input.is_open())
    throw std::runtime_error("Cannot open config file: " + path);
  auto config = keen_pbr3::parse_config(input);
  keen_pbr3::validate_config(config);
  return config;
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
        !opts.run_test_routing && !opts.hash_password &&
        !opts.run_dnsmasq_config) {
      print_usage(argv[0]);
      return 0;
    }

    // Initialize logger
    auto &logger = keen_pbr3::Logger::instance();
    logger.set_level(keen_pbr3::parse_log_level(opts.log_level));
    logger.set_target(opts.log_target.empty()
                          ? (opts.run_service
                                 ? keen_pbr3::LogTarget::syslog_only
                                 : keen_pbr3::LogTarget::stderr_only)
                          : keen_pbr3::parse_log_target(opts.log_target));

    if (opts.hash_password) {
      const auto password = read_secret("Password: ");
      const auto confirmation = read_secret("Confirm password: ");
      if (password.empty()) throw std::runtime_error("Password cannot be empty");
      if (password != confirmation) throw std::runtime_error("Passwords do not match");
      const auto verifier = keen_pbr3::auth::generate_password_hash(password);
      if (!opts.update_password) {
        std::cout << "Password hash: " << verifier << '\n';
        return 0;
      }
      auto root = nlohmann::json::parse(read_file(opts.config_path));
      root["api"]["enabled"] = true;
      root["api"]["authentication"]["enabled"] = true;
      root["api"]["authentication"]["password_hash"] = verifier;
      const auto body = root.dump(1, '\t') + "\n";
      auto updated = keen_pbr3::parse_config(body);
      keen_pbr3::validate_config(updated);
      keen_pbr3::write_config_atomically(opts.config_path, body);
      std::cout << "Authentication password updated in " << opts.config_path << ".\n"
                << "Restart the keen-pbr service for authentication changes to take effect.\n";
      return 0;
    }

    if (opts.run_dnsmasq_config) {
      // Runs inside dnsmasq's startup (conf-script): stdout is parsed by
      // dnsmasq, a failing script would stop DNS for the whole LAN, so always
      // exit 0 and report problems as a comment line (details go to stderr).
      std::ios::sync_with_stdio(false);
      if (opts.resolver_type != "dnsmasq") {
        std::cout << "# keen-pbr: unsupported resolver type\n";
        std::cout.flush();
        return 0;
      }
      try {
        const keen_pbr3::Config config = [&opts] {
          std::ifstream config_stream(opts.config_path);
          if (!config_stream.is_open()) {
            throw std::runtime_error("Cannot open config file: " + opts.config_path);
          }
          return keen_pbr3::parse_config(config_stream);
        }();
        keen_pbr3::validate_config(config);
        if (keen_pbr3::effective_resolver_integration(config) ==
            keen_pbr3::ResolverIntegrationMode::NONE) {
          std::cout << "# keen-pbr: dns.resolver_integration is none\n";
          std::cout.flush();
          return 0;
        }
        const auto dns = config.dns.value_or(keen_pbr3::DnsConfig{});
        const auto lists =
            config.lists.value_or(std::map<std::string, keen_pbr3::ListConfig>{});
        const auto cache_dir = config.daemon.value_or(keen_pbr3::DaemonConfig{})
                                   .cache_dir.value_or("/var/cache/keen-pbr");
        // CacheManager's constructor does not touch the disk; only cached
        // lists are read and nothing is downloaded.
        keen_pbr3::CacheManager cache(cache_dir,
                                      keen_pbr3::max_file_size_bytes(config));
        keen_pbr3::ListStreamer streamer(cache);
        const keen_pbr3::DnsServerRegistry registry(dns);
        keen_pbr3::DnsmasqGenerator generator(registry, streamer, dns, lists);
        generator.generate(std::cout);
        std::cout.flush();
      } catch (...) {
        std::string message;
        try {
          throw;
        } catch (const keen_pbr3::ConfigValidationError &e) {
          message = e.what();
          for (const auto &issue : e.issues()) {
            message += "; " + issue.path + ": " + issue.message;
          }
        } catch (const std::exception &e) {
          message = e.what();
        } catch (...) {
          message = "unknown error";
        }
        std::cerr << "keen-pbr dnsmasq-config failed: " << message << '\n';
        for (char &c : message) {
          if (c == '\n' || c == '\r') c = ' ';
        }
        std::cout << "# keen-pbr: dnsmasq-config failed: " << message << '\n';
        std::cout.flush();
      }
      return 0;
    }

    // Deprecated no-op stub: old installs may still call it.
    if (opts.resolver_config_hash) {
      return 0;
    }

    if (opts.run_status || opts.download_lists ||
        opts.run_test_routing) {
      if (opts.config_path != KEEN_PBR_DEFAULT_CONFIG_PATH) {
        throw std::runtime_error(
            "--config is only supported with the service command");
      }
      const std::string operation =
          opts.run_status
              ? "status"
              : (opts.download_lists ? "download" : "test-routing");
      const auto response = opts.download_lists
          ? keen_pbr3::ipc::request_control(
                KEEN_PBR_CONTROL_SOCKET,
                {{"protocol_version", keen_pbr3::ipc::kControlProtocolVersion},
                 {"request_id", "cli-" + operation},
                 {"operation", operation},
                 {"reload", opts.download_reload}})
          : request_control_state(operation, opts.test_routing_target);
      if (!response.value("ok", false)) {
        const auto& error = response.value("error", nlohmann::json::object());
        std::cerr << "keen-pbr " << operation << ": "
                  << error.value("code", "daemon_error") << ": "
                  << error.value("message", "control request failed") << '\n';
        return 1;
      }
      if (opts.download_lists) {
        std::cout << response.dump() << '\n';
        return 0;
      }
      const auto &state = response.at("result");
      const auto config_path = state.value(
          "config_path", std::string(KEEN_PBR_DEFAULT_CONFIG_PATH));
      const auto config = load_committed_config(config_path);
      if (opts.run_status) {
        if (!state.contains("routing_health") || state.at("routing_health").is_null()) {
          return keen_pbr3::run_status_command(config, config_path,
                                               nullptr);
        }
        return keen_pbr3::run_status_command(
            config, config_path, state.at("routing_health"));
      }
      const auto rules = parse_realized_rules(state);
      const auto cache_dir = config.daemon.value_or(keen_pbr3::DaemonConfig{})
                                 .cache_dir.value_or("/var/cache/keen-pbr");
      keen_pbr3::CacheManager cache(cache_dir,
                                    keen_pbr3::max_file_size_bytes(config));
      if (opts.run_test_routing) {
        return keen_pbr3::run_test_routing_command(
            config, cache, opts.test_routing_target, rules);
      }
      return 0;
    }

    // Parse directly from the file and destroy the stream before the daemon is
    // constructed, keeping neither raw JSON nor the stream buffer alive.
    keen_pbr3::Config config = [&opts] {
      std::ifstream config_stream(opts.config_path);
      if (!config_stream.is_open()) {
        throw std::runtime_error("Cannot open config file: " + opts.config_path);
      }
      return keen_pbr3::parse_config(config_stream);
    }();
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
      daemon_opts.use_raw6_prerouting = opts.use_raw6_prerouting;

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
