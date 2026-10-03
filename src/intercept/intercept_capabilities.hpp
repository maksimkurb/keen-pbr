#pragma once

#include "../firewall/firewall.hpp"

#include <functional>
#include <string>

namespace keen_pbr3 {

// What the kernel offers for traffic interception.  `nfqueue` includes the
// conntrack match the DNS hold rule needs; `connbytes` is only meaningful for
// the iptables backend (nftables uses `ct original packets`).
struct InterceptCapabilities {
    bool nfqueue{false};
    bool nflog{false};
    bool connbytes{false};
    // Human readable list of what is missing (empty when everything is there).
    std::string reason;
};

// Injectable inputs of the probe so unit tests can use fake /proc files.
struct InterceptProbeEnv {
    std::string ip_targets{"/proc/net/ip_tables_targets"};
    std::string ip6_targets{"/proc/net/ip6_tables_targets"};
    std::string ip_matches{"/proc/net/ip_tables_matches"};
    std::string ip6_matches{"/proc/net/ip6_tables_matches"};
    // Loads a kernel module; failures are ignored.  Empty: `modprobe <module>`.
    std::function<void(const std::string& module)> modprobe;
};

// iptables: reads the kernel's loaded target/match lists, trying one
// `modprobe` per missing module and re-reading.  nftables: nft_queue/nft_log/
// nft_ct autoload, so everything is reported as available.
InterceptCapabilities probe_intercept_capabilities(FirewallBackend backend,
                                                   bool ipv6_enabled,
                                                   const InterceptProbeEnv& env = {});

// `ct original packets` (nftables sniff) needs conntrack accounting; iptables
// connbytes enables it itself.  Returns false (and logs) on failure.
bool enable_conntrack_accounting(
    const std::string& path = "/proc/sys/net/netfilter/nf_conntrack_acct");

} // namespace keen_pbr3
