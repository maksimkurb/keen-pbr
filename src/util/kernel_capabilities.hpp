#pragma once

#include "../firewall/firewall.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace keen_pbr3 {

// Kernel / system capability checks run ONCE per process, at service start, and
// everything afterwards (config validation, firewall prepare/apply, intercept
// resolution, status) only reads the answers below.  There is no re-probe:
// a kernel change needs a reboot, a binary change a service restart.
//
// Two layers so that nothing has to spawn a process just to learn what exists:
//  - HostTools: which executables the backends need are present.  Pure PATH
//    lookups (no process is started), so config validation can use it.
//  - KernelCapabilities: what the kernel/userspace pair can do.  Starts a few
//    read-only helper processes (`ip6tables -S`, `*-restore --test`, `nft -c`)
//    on its first use, which the daemon forces at service start.
// Interception probes (netlink set/conntrack/queue checks, modules) need the
// selected firewall backend and live in InterceptStartupProbe, built by the
// daemon right after this snapshot.

struct HostTools {
    bool nft{false};
    bool iptables{false};
    bool iptables_restore{false};
    bool ip6tables{false};
    bool ip6tables_restore{false};
    bool ipset{false};

    // Detected backend: nftables wins when both are installed.
    std::optional<FirewallBackend> detected_backend() const;
    // Names of the executables `backend` needs that are missing.  IPv6 tools are
    // optional (their absence turns IPv6 off), so they are not listed.
    std::string missing_required(FirewallBackend backend) const;
};

struct KernelCapabilities {
    HostTools tools;
    // AF_INET6 sockets work at all.
    bool system_ipv6{false};
    // The IPv6 half of each backend works (ip6tables mangle + restore / nft inet).
    bool iptables_ipv6{false};
    bool nftables_ipv6{false};
    // iptables ownership comments (xt_comment registration + restore grammar).
    bool xt_comment_v4{false};
    bool xt_comment_v6{false};
    // iptables `-j NFLOG --nflog-size` (iptables >= 1.6.0).
    bool nflog_size_v4{false};
    bool nflog_size_v6{false};
    // iptables `-m statistic --mode random` (load balancing).  One module
    // serves IPv4 and IPv6.
    bool xt_statistic{false};

    bool firewall_ipv6(FirewallBackend backend) const {
        return backend == FirewallBackend::iptables ? iptables_ipv6 : nftables_ipv6;
    }
};

// Executables present on PATH, looked up once.
const HostTools& host_tools();

// The snapshot; probed on first use, then immutable.  Never null.
std::shared_ptr<const KernelCapabilities> kernel_capabilities();

// Runs the probes and returns the result without installing it.
KernelCapabilities probe_kernel_capabilities(const HostTools& tools);

// How many times the probes above ran in this process (tests assert 1).
std::uint64_t kernel_capability_probe_runs();

#ifdef KEEN_PBR3_TESTING
// Replaces the snapshot / the tools (a snapshot's tools are not used to answer
// host_tools()).  nullopt drops the override so the next use probes again.
void set_kernel_capabilities_for_tests(std::optional<KernelCapabilities> snapshot);
void set_host_tools_for_tests(std::optional<HostTools> tools);
// Forgets every cached answer (tests that change PATH).
void reset_kernel_capabilities_for_tests();
#endif

} // namespace keen_pbr3
