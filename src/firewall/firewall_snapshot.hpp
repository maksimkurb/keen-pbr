#pragma once

#include "firewall_physical.hpp"
#include "firewall_verifier.hpp"

#include <memory>
#include <string>

namespace keen_pbr3 {

// One read of the kernel firewall state as a backend-neutral PhysicalRuleset
// (see firewall_physical.hpp).  It carries no policy knowledge: the verifier
// diffs it against the ruleset expected from the active apply.
struct FirewallSnapshot {
    FirewallBackend backend{FirewallBackend::iptables};
    bool available{false};
    std::string error;
    PhysicalRuleset ruleset;
};

class FirewallSnapshotInspector {
public:
    virtual ~FirewallSnapshotInspector() = default;
    // Reads only the tables that can hold keen-pbr rules: iptables mangle
    // (plus raw per family in RAW PREROUTING mode) for IPv4 and, when
    // `ipv6_enabled`, IPv6; the nft KeenPbrTable without set elements.
    virtual FirewallSnapshot inspect(bool ipv6_enabled) const = 0;
};

FirewallSnapshot inspect_iptables_snapshot(
    const CommandRunner& runner, RawPreroutingMode raw_prerouting = {},
    bool ipv6_enabled = true);
FirewallSnapshot inspect_nftables_snapshot(const CommandRunner& runner);

std::unique_ptr<FirewallSnapshotInspector> create_firewall_snapshot_inspector(
    FirewallBackend backend, RawPreroutingMode raw_prerouting = {},
    CommandRunner runner = run_command_capture);

} // namespace keen_pbr3
