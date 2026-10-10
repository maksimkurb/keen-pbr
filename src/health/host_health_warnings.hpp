#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../routing/netlink.hpp"
#include "routing_health.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Non-blocking host configuration warnings for load-balancing WANs. These
// never influence overall_ok and never change sysctls or NAT.

enum class NatCoverage { covered, partial, missing };

struct NatCoverageResult {
    NatCoverage coverage{NatCoverage::missing};
    // For partial coverage: the first restriction seen
    // (for example "-m mark --mark 0x40000/0xff0000" or "-s 172.17.0.0/16").
    std::string restriction;
};

// An IPv4 network (host byte order).
struct Ipv4Net {
    uint32_t addr{0};
    uint32_t mask{0};
};

std::optional<Ipv4Net> parse_ipv4_net(const std::string& text);

// IPv4 networks of the named interfaces (from netlink address dumps).
std::vector<Ipv4Net> interface_subnets(
    const std::vector<DumpedInterface>& interfaces,
    const std::vector<std::string>& names);

// Pure parser over `iptables-save -t nat` / `iptables -t nat -S` text.
// Only rules in chains reachable from POSTROUTING count. Rules with any match
// other than -o/-s/-d (or with a positive -d) are restrictions and yield at
// best a partial result. `-s` rules are judged against `lan_subnets`: empty
// means unknown, in which case -s rules count as covering.
NatCoverageResult nat_coverage_for_interface(
    const std::string& nat_text, const std::string& interface,
    const std::vector<Ipv4Net>& lan_subnets = {});

struct BalanceCandidate {
    std::string interface;
    std::string outbound_tag;
};

// Member outbounds of every balance outbound that resolve to a concrete
// interface (type interface). Deduplicated by interface name.
std::vector<BalanceCandidate> balance_candidate_interfaces(const Config& config);

// Reads a file; nullopt when it cannot be read.
using ProcFileReader =
    std::function<std::optional<std::string>(const std::string& path)>;
std::optional<std::string> read_proc_file(const std::string& path);

std::vector<HealthWarning> evaluate_rp_filter_warnings(
    const std::vector<BalanceCandidate>& candidates, const ProcFileReader& reader);

std::vector<HealthWarning> evaluate_nat_warnings(
    const std::vector<BalanceCandidate>& candidates, const std::string& nat_text,
    const std::vector<Ipv4Net>& lan_subnets = {});

// Small inputs copied out of the config so the health worker does not need a
// full Config copy.
struct HostHealthInputs {
    std::vector<BalanceCandidate> candidates;
    std::vector<std::string> inbound_interfaces;
};
HostHealthInputs host_health_inputs(const Config& config);

// Time-limited, thread-safe cache of the `iptables -t nat -S` output.
class NatTableCache {
public:
    using Clock = std::chrono::steady_clock;
    explicit NatTableCache(std::chrono::seconds lifetime = std::chrono::seconds{30})
        : lifetime_(lifetime) {}
    // Returns cached text or reads it through `runner`; nullopt when unreadable
    // (failures are not cached).
    std::optional<std::string> get(const CommandRunner& runner);
    void invalidate();

private:
    std::chrono::seconds lifetime_;
    std::mutex mutex_;
    std::optional<std::string> text_;
    Clock::time_point read_at_{};
};

// Collects all host warnings. NAT is only checked on the iptables backend and
// silently skipped when the nat table cannot be read.
std::vector<HealthWarning> collect_host_health_warnings(
    const HostHealthInputs& inputs,
    const std::vector<DumpedInterface>& interfaces, FirewallBackend backend,
    NatTableCache& nat_cache, const CommandRunner& runner,
    const ProcFileReader& reader);

// Stable one-line-per-warning key list, used to log only on changes.
std::vector<std::string> health_warning_keys(const std::vector<HealthWarning>& w);

} // namespace keen_pbr3
