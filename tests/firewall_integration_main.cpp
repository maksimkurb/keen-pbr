#include "../src/cache/cache_manager.hpp"
#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/firewall/firewall.hpp"
#include "../src/firewall/firewall_runtime.hpp"
#include "../src/health/routing_health_checker.hpp"
#include "../src/health/url_tester.hpp"
#include "../src/log/logger.hpp"
#include "../src/routing/firewall_state.hpp"
#include "../src/routing/netlink.hpp"
#include "../src/routing/policy_rule.hpp"
#include "../src/routing/route_table.hpp"
#include "../src/util/format_compat.hpp"
#include "../src/util/safe_exec.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {

struct CliOptions {
    std::string config_path;
    std::string backend{"iptables"};
    std::string mode{"destructive"};
    std::string log_level{"info"};
    bool run_urltest_probes{false};
};

struct RuntimeCleanup {
    RouteTable& route_table;
    PolicyRuleManager& policy_rules;
    Firewall& firewall;

    ~RuntimeCleanup() {
        try {
            firewall.cleanup();
        } catch (...) {
        }
        try {
            policy_rules.clear();
        } catch (...) {
        }
        try {
            route_table.clear();
        } catch (...) {
        }
    }
};

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " --config <path> --backend <iptables|nftables> "
        << "--mode <destructive|preserve-sets> [--run-urltest-probes] [--log-level <lvl>]\n";
}

CliOptions parse_args(int argc, char* argv[]) {
    CliOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--config requires a value");
            }
            options.config_path = argv[++i];
        } else if (arg == "--backend") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires a value");
            }
            options.backend = argv[++i];
        } else if (arg == "--mode") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--mode requires a value");
            }
            options.mode = argv[++i];
        } else if (arg == "--log-level") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--log-level requires a value");
            }
            options.log_level = argv[++i];
        } else if (arg == "--run-urltest-probes") {
            options.run_urltest_probes = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }

    if (options.config_path.empty()) {
        throw std::runtime_error("--config is required");
    }

    return options;
}

FirewallBackendPreference parse_backend_preference(const std::string& value) {
    if (value == "iptables") {
        return FirewallBackendPreference::iptables;
    }
    if (value == "nftables") {
        return FirewallBackendPreference::nftables;
    }
    throw std::runtime_error("Unsupported backend: " + value);
}

FirewallApplyMode parse_apply_mode(const std::string& value) {
    if (value == "destructive") {
        return FirewallApplyMode::Destructive;
    }
    if (value == "preserve-sets") {
        return FirewallApplyMode::PreserveSets;
    }
    throw std::runtime_error("Unsupported mode: " + value);
}

api::DaemonConfigFirewallBackend parse_backend_config_value(const std::string& value) {
    if (value == "iptables") {
        return api::DaemonConfigFirewallBackend::IPTABLES;
    }
    if (value == "nftables") {
        return api::DaemonConfigFirewallBackend::NFTABLES;
    }
    throw std::runtime_error("Unsupported backend: " + value);
}

std::vector<const Outbound*> find_urltest_children(const std::vector<Outbound>& outbounds,
                                                   const Outbound& urltest) {
    struct GroupRef {
        size_t index;
        uint32_t weight;
    };

    std::vector<GroupRef> groups;
    const auto& outbound_groups = urltest.outbound_groups.value_or(std::vector<OutboundGroup>{});
    groups.reserve(outbound_groups.size());
    for (size_t i = 0; i < outbound_groups.size(); ++i) {
        groups.push_back(GroupRef{
            .index = i,
            .weight = static_cast<uint32_t>(outbound_groups[i].weight.value_or(1)),
        });
    }

    std::sort(groups.begin(), groups.end(), [](const GroupRef& lhs, const GroupRef& rhs) {
        return lhs.weight < rhs.weight;
    });

    std::vector<const Outbound*> ordered_children;
    for (const auto& group_ref : groups) {
        for (const auto& child_tag : outbound_groups[group_ref.index].outbounds) {
            for (const auto& outbound : outbounds) {
                if (outbound.tag == child_tag) {
                    ordered_children.push_back(&outbound);
                    break;
                }
            }
        }
    }
    return ordered_children;
}

std::optional<std::string> select_urltest_child(const Outbound& urltest,
                                                const std::vector<const Outbound*>& ordered_children,
                                                const OutboundMarkMap& marks,
                                                URLTester& tester) {
    struct CandidateResult {
        std::string child_tag;
        uint32_t latency_ms{0};
        uint32_t group_weight{0};
    };

    std::vector<CandidateResult> successful_results;
    const auto& groups = urltest.outbound_groups.value_or(std::vector<OutboundGroup>{});
    const uint32_t timeout_ms = static_cast<uint32_t>(
        urltest.circuit_breaker.value_or(CircuitBreakerConfig{}).timeout_ms.value_or(5000));
    const RetryConfig retry = urltest.retry.value_or(RetryConfig{});

    for (const auto& group : groups) {
        for (const auto& child_tag : group.outbounds) {
            auto mark_it = marks.find(child_tag);
            if (mark_it == marks.end()) {
                continue;
            }

            URLTestResult result =
                tester.test(urltest.url.value_or(""), mark_it->second, timeout_ms, retry);
            if (result.success) {
                successful_results.push_back(CandidateResult{
                    .child_tag = child_tag,
                    .latency_ms = result.latency_ms,
                    .group_weight = static_cast<uint32_t>(group.weight.value_or(1)),
                });
            }
        }
    }

    if (successful_results.empty()) {
        return std::nullopt;
    }

    const uint32_t best_weight = std::min_element(
        successful_results.begin(),
        successful_results.end(),
        [](const CandidateResult& lhs, const CandidateResult& rhs) {
            return lhs.group_weight < rhs.group_weight;
        })->group_weight;

    uint32_t min_latency = std::numeric_limits<uint32_t>::max();
    for (const auto& result : successful_results) {
        if (result.group_weight == best_weight) {
            min_latency = std::min(min_latency, result.latency_ms);
        }
    }

    const uint32_t tolerance = static_cast<uint32_t>(urltest.tolerance_ms.value_or(100));
    for (const auto* child : ordered_children) {
        for (const auto& result : successful_results) {
            if (result.group_weight != best_weight || result.child_tag != child->tag) {
                continue;
            }
            if (result.latency_ms <= min_latency + tolerance) {
                return result.child_tag;
            }
        }
    }

    return std::nullopt;
}

std::map<std::string, std::string> probe_urltests(const Config& config,
                                                  const OutboundMarkMap& marks) {
    std::map<std::string, std::string> selections;
    URLTester tester;
    const auto& outbounds = config.outbounds.value_or(std::vector<Outbound>{});

    for (const auto& outbound : outbounds) {
        if (outbound.type != OutboundType::URLTEST) {
            continue;
        }

        const auto ordered_children = find_urltest_children(outbounds, outbound);
        auto selection = select_urltest_child(outbound, ordered_children, marks, tester);
        if (selection.has_value()) {
            selections[outbound.tag] = *selection;
        }
    }

    return selections;
}

bool report_has_failures(const RoutingHealthReport& report) {
    if (!report.error.empty()) {
        return true;
    }
    if (!report.overall_ok) {
        return true;
    }
    if (!report.firewall_chain.chain_present || !report.firewall_chain.prerouting_hook_present) {
        return true;
    }

    for (const auto& check : report.firewall_rules) {
        if (check.status != CheckStatus::ok) {
            return true;
        }
    }
    for (const auto& check : report.route_tables) {
        if (check.status != CheckStatus::ok) {
            return true;
        }
    }
    for (const auto& check : report.policy_rules) {
        if (check.status != CheckStatus::ok) {
            return true;
        }
    }
    return false;
}

void print_command_dump(const std::string& label,
                        const std::vector<std::string>& command) {
    const auto result = safe_exec_capture(command, /*suppress_stderr=*/false, 1024 * 1024);
    std::cerr << "===== " << label << " =====\n";
    std::cerr << "command: " << safe_exec_command_string(command) << "\n";
    std::cerr << "exit_code: " << result.exit_code << "\n";
    if (result.truncated) {
        std::cerr << "output: [truncated]\n";
    }
    if (result.stdout_output.empty()) {
        std::cerr << "(no output)\n";
    } else {
        std::cerr << result.stdout_output;
        if (result.stdout_output.back() != '\n') {
            std::cerr << "\n";
        }
    }
}

void dump_firewall_state(FirewallBackend backend) {
    if (backend == FirewallBackend::iptables) {
        print_command_dump("iptables-save", {"iptables-save", "-t", "mangle"});
        print_command_dump("ip6tables-save", {"ip6tables-save", "-t", "mangle"});
        print_command_dump("ipset-list", {"ipset", "list"});
        return;
    }

    print_command_dump("nft-list-ruleset", {"nft", "list", "ruleset"});
}

void print_report(const RoutingHealthReport& report) {
    if (!report.error.empty()) {
        std::cerr << "health-check error: " << report.error << "\n";
        return;
    }

    if (!report.firewall_chain.chain_present || !report.firewall_chain.prerouting_hook_present) {
        std::cerr << "firewall chain check failed: chain_present="
                  << (report.firewall_chain.chain_present ? "true" : "false")
                  << " prerouting_hook_present="
                  << (report.firewall_chain.prerouting_hook_present ? "true" : "false");
        if (!report.firewall_chain.detail.empty()) {
            std::cerr << " detail=" << report.firewall_chain.detail;
        }
        std::cerr << "\n";
    }

    for (const auto& check : report.firewall_rules) {
        if (check.status == CheckStatus::ok) {
            continue;
        }
        std::cerr << "firewall rule failed: set=" << check.set_name
                  << " action=" << check.action
                  << " status=" << static_cast<int>(check.status);
        if (!check.detail.empty()) {
            std::cerr << " detail=" << check.detail;
        }
        std::cerr << "\n";
    }

    for (const auto& check : report.route_tables) {
        if (check.status == CheckStatus::ok) {
            continue;
        }
        std::cerr << "route check failed: table=" << check.table_id
                  << " outbound=" << check.outbound_tag;
        if (!check.detail.empty()) {
            std::cerr << " detail=" << check.detail;
        }
        std::cerr << "\n";
    }

    for (const auto& check : report.policy_rules) {
        if (check.status == CheckStatus::ok) {
            continue;
        }
        std::cerr << "policy rule failed: fwmark=" << check.fwmark
                  << " table=" << check.expected_table;
        if (!check.detail.empty()) {
            std::cerr << " detail=" << check.detail;
        }
        std::cerr << "\n";
    }
}

} // namespace

int run_firewall_integration(int argc, char* argv[]) {
    CliOptions options = parse_args(argc, argv);

    auto& logger = Logger::instance();
    logger.set_level(parse_log_level(options.log_level));

    Config config = parse_and_validate_config(read_file(options.config_path));
    if (!config.daemon.has_value()) {
        config.daemon = DaemonConfig{};
    }
    config.daemon->firewall_backend = parse_backend_config_value(options.backend);

    const auto mode = parse_apply_mode(options.mode);
    const auto marks = allocate_outbound_marks(
        config.fwmark.value_or(FwmarkConfig{}),
        config.outbounds.value_or(std::vector<Outbound>{}));

    CacheManager cache_manager(
        config.daemon.value_or(DaemonConfig{}).cache_dir.value_or("/var/cache/keen-pbr"),
        max_file_size_bytes(config));
    cache_manager.ensure_dir();

    const auto urltest_selections =
        options.run_urltest_probes ? probe_urltests(config, marks) : std::map<std::string, std::string>{};

    NetlinkManager netlink;
    RouteTable route_table(netlink);
    PolicyRuleManager policy_rules(netlink);
    auto firewall = create_firewall(parse_backend_preference(options.backend));
    RuntimeCleanup cleanup{route_table, policy_rules, *firewall};

    FirewallState firewall_state;
    firewall_state.set_outbound_marks(marks);
    for (const auto& [tag, child] : urltest_selections) {
        firewall_state.set_urltest_selection(tag, child);
    }

    populate_routing_state(
        config,
        marks,
        route_table,
        policy_rules,
        [&netlink](const Outbound& outbound) {
            return is_interface_outbound_reachable(outbound, netlink);
        },
        &urltest_selections);

    firewall_state.set_rules(apply_runtime_firewall(
        config,
        marks,
        urltest_selections,
        cache_manager,
        *firewall,
        mode));

    const RoutingHealthReport report = build_routing_health_report(
        firewall->backend(),
        firewall_state,
        route_table.get_routes(),
        policy_rules.get_rules(),
        netlink);

    if (report_has_failures(report)) {
        print_report(report);
        dump_firewall_state(firewall->backend());
        return 1;
    }

    std::cout << "ok backend=" << options.backend
              << " mode=" << options.mode
              << " config=" << options.config_path << "\n";
    return 0;
}

} // namespace keen_pbr3

int main(int argc, char* argv[]) {
    struct CurlGuard {
        CurlGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~CurlGuard() { curl_global_cleanup(); }
    };
    CurlGuard curl_guard;

    try {
        return keen_pbr3::run_firewall_integration(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
