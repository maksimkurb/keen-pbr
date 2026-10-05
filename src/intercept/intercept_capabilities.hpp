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
    nfnl::ProbeResult timeout_update;  // nft: non-exclusive NEWSETELEM extends an existing timeout

    // Every probed feature in a stable order (for health output and logs).
    std::vector<Item> items() const;

    // Keeps `previous` results for every check this probe did not run (its
    // stage has not happened yet in this round), so a re-probe does not turn a
    // known kernel verdict back into `not_run`.  Results this probe produced
    // always win.  Blocking verdicts are not carried: they disable the stage
    // that would re-run the check, so a refresh must retry them.  fail_open
    // and replacement travel with a non-blocking nfqueue (same bind; a
    // blocking fail_open only warns).
    void carry_forward(const InterceptRuntimeProbe& previous);

    // Forgets listener verdicts that blocked (a failed queue/group bind) so the
    // next bind is tried again.  Kernel facts probed at service start are never
    // touched, nothing is probed: a bind failure is not a capability verdict,
    // it only says that bind did not work.  fail_open and replacement go with
    // the nfqueue bind they came from.
    void forget_blocking_listener_results();
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
    // Attempt to load kernel modules (nfnetlink_queue, nfnetlink_log, etc).
    // When false, probe_intercept_capabilities only reads /proc and runs functional
    // probes; all modprobe calls are skipped.  Set to true for the first probe
    // after daemon startup, false for subsequent re-probes.
    bool load_modules{true};
    // Creates / removes the throw-away dynamic set the set-write and
    // timeout-update probes run against.  Empty: `ipset create|destroy` (iptables)
    // or `nft` with a scratch table (nftables).  create returns false when the
    // set cannot be created (the probes are then `skipped`, never `error`).
    std::function<bool()> create_scratch_set;
    std::function<void()> destroy_scratch_set;
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

// What the daemon measures once at service start for interception.  Both
// variants share one set of functional probes; they differ only in whether the
// IPv6 half of the iptables modules is required, so a later `daemon.ipv6_enabled`
// toggle picks the matching one without probing again.
struct InterceptStartupProbe {
    InterceptCapabilities with_ipv6;  // IPv4 + IPv6 requirements (== ipv4_only when IPv6 is unusable)
    InterceptCapabilities ipv4_only;
    const InterceptCapabilities& for_ipv6(bool ipv6_enabled) const {
        return ipv6_enabled ? with_ipv6 : ipv4_only;
    }
};

// The one interception probe: modprobe (first and only time), /proc reads,
// netlink set-backend and conntrack checks, and the set-write and nft
// timeout-update checks on a throw-away set (see probe_intercept_scratch_set).
InterceptStartupProbe probe_intercept_startup(FirewallBackend backend, bool ipv6_supported,
                                              const InterceptProbeEnv& env = {});

// Set-write and nft timeout-update probes against a scratch dynamic set that
// only this probe owns (`ipset create kpbr4d_keenpbrprobe` / nft table
// KeenPbrProbe), removed again afterwards.  Replaces the per-apply check on the
// real sets: the capability under test is the kernel's, so it is measured once
// at service start.  `skipped` (never blocking) when the scratch set cannot be
// created.  Never throws.
struct SetFeatureProbe {
    nfnl::ProbeResult set_write;
    nfnl::ProbeResult timeout_update;
};
SetFeatureProbe probe_intercept_scratch_set(FirewallBackend backend,
                                            const InterceptProbeEnv& env = {});

// Functional test that the daemon can write a dynamic set: adds and removes a
// reserved documentation address (192.0.2.255 /
// 2001:db8::ffff, 1 s timeout) on one `kpbr4d_*` / `kpbr6d_*` set.  `skipped`
// when `sets` holds no dynamic set.  Never throws.
nfnl::ProbeResult probe_intercept_set_write(FirewallBackend backend,
                                            const std::vector<FirewallSetDeclaration>& sets,
                                            const InterceptProbeEnv& env = {});

// nftables only: whether the kernel extends the timeout of an existing element
// in place (feature "nft_timeout_update", see nfnl::probe_nft_timeout_update).
// Uses a reserved documentation address (192.0.2.254 / 2001:db8::fffe) on one
// dynamic set and always removes it again.  `not_run` for other backends or
// when runtime probes are off, `skipped` when no dynamic set exists.  Never
// blocks interception: anything but `ok` just keeps the delete+add refresh.
nfnl::ProbeResult probe_intercept_nft_timeout_update(
    FirewallBackend backend, const std::vector<FirewallSetDeclaration>& sets,
    const InterceptProbeEnv& env = {});

// `ct original packets` (nftables sniff) needs conntrack accounting; iptables
// connbytes enables it itself.  Returns false (and logs) on failure.
bool enable_conntrack_accounting(
    const std::string& path = "/proc/sys/net/netfilter/nf_conntrack_acct");

} // namespace keen_pbr3
