#pragma once

#include "../firewall/firewall.hpp"
#include "../firewall/firewall_plan.hpp"
#include "../netfilter/kernel_probe.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Outcome of exercising each kernel primitive for real (see
// netfilter/kernel_probe.hpp).  Filled in stages: set backend and conntrack at
// capability probing, listener bind and fail-open when the service starts, the
// set write test after the firewall created the dynamic sets.  A stage that has
// not run stays `not_run`, which never blocks anything.
struct InterceptRuntimeProbe {
    struct Item {
        std::string feature;
        nfnl::ProbeResult result;
    };

    std::string kernel_release;   // informational only; never used for decisions
    int ipset_protocol{0};        // kernel ipset protocol (ipset backend), 0 = unknown
    nfnl::ProbeResult set_backend;   // ipset protocol >= 6 / nf_tables netlink
    nfnl::ProbeResult nfqueue;       // queue bind + params
    nfnl::ProbeResult fail_open;     // NFQA_CFG_F_FAIL_OPEN accepted by the kernel
    nfnl::ReplacementCapability replacement{nfnl::ReplacementCapability::unknown};
    nfnl::ProbeResult nflog;         // NFLOG group bind
    nfnl::ProbeResult set_write;     // test add+del on a dynamic set
    nfnl::ProbeResult conntrack;     // ctnetlink dump

    // Every probed feature in a stable order (for health output and logs).
    std::vector<Item> items() const;
};

// What the kernel offers for traffic interception.  `nfqueue` includes the
// conntrack match the DNS hold rule needs; `connbytes` is only meaningful for
// the iptables backend (nftables uses `ct original packets`).
struct InterceptCapabilities {
    bool nfqueue{false};
    bool nflog{false};
    bool connbytes{false};
    // Human readable list of what is missing (empty when everything is there).
    std::string reason;
    // Functional probe results; see InterceptRuntimeProbe.
    InterceptRuntimeProbe probe;
};

// Injectable inputs of the probe so unit tests can use fake /proc files.
struct InterceptProbeEnv {
    std::string ip_targets{"/proc/net/ip_tables_targets"};
    std::string ip6_targets{"/proc/net/ip6_tables_targets"};
    std::string ip_matches{"/proc/net/ip_tables_matches"};
    std::string ip6_matches{"/proc/net/ip6_tables_matches"};
    // Loads a kernel module; failures are ignored.  Empty: `modprobe <module>`.
    std::function<void(const std::string& module)> modprobe;
    // Run the functional netlink probes (set backend, ctnetlink) in addition to
    // the /proc checks.  Tests that only exercise the /proc logic turn it off.
    bool runtime_probes{true};
    // Transport factory for the probes; empty: a fresh NETLINK_NETFILTER socket.
    // May throw nfnl::NlSocketError, which is reported as a probe error.
    std::function<std::unique_ptr<nfnl::SetWriterTransport>()> make_transport;
    // Kernel release string for the report; empty: uname(2).
    std::function<std::string()> kernel_release;
    std::string nft_table{"KeenPbrTable"};
    int probe_timeout_ms{nfnl::kDefaultProbeTimeoutMs};
};

// iptables: reads the kernel's loaded target/match lists, trying one
// `modprobe` per missing module and re-reading.  nftables: everything is
// reported as available; a missing nfnetlink_queue/nfnetlink_log module shows
// up when the listener binds.  Both backends first try to `modprobe`
// nfnetlink_queue and nfnetlink_log (plus nft_queue/nft_log/nft_ct for
// nftables), ignoring failures.
InterceptCapabilities probe_intercept_capabilities(FirewallBackend backend,
                                                   bool ipv6_enabled,
                                                   const InterceptProbeEnv& env = {});

// Functional test that the daemon can write the dynamic sets the firewall just
// created: adds and removes a reserved documentation address (192.0.2.255 /
// 2001:db8::ffff, 1 s timeout) on one `kpbr4d_*` / `kpbr6d_*` set.  `skipped`
// when `sets` holds no dynamic set.  Never throws.
nfnl::ProbeResult probe_intercept_set_write(FirewallBackend backend,
                                            const std::vector<FirewallSetDeclaration>& sets,
                                            const InterceptProbeEnv& env = {});

// `ct original packets` (nftables sniff) needs conntrack accounting; iptables
// connbytes enables it itself.  Returns false (and logs) on failure.
bool enable_conntrack_accounting(
    const std::string& path = "/proc/sys/net/netfilter/nf_conntrack_acct");

} // namespace keen_pbr3
