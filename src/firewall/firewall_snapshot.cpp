#include "firewall_snapshot.hpp"

#include <utility>

namespace keen_pbr3 {
namespace {

bool read_failed(const CommandResult& result) {
    return result.exit_code != 0 || result.truncated;
}

// Reads `<command> -t <table> -S` and merges the parsed table.  The -S form is
// what the backend lifecycle already relies on, so no extra tool is needed.
bool read_iptables_table(const CommandRunner& runner, const char* command,
                         const char* table, PhysicalTable physical_table,
                         FirewallFamily family, PhysicalRuleset& into) {
    const auto result = runner({command, "-t", table, "-S"});
    if (read_failed(result)) return false;
    append_physical_ruleset(
        into, parse_iptables_save(result.stdout_output, family, physical_table));
    return true;
}

FirewallSnapshot inspect_iptables_snapshot_impl(const CommandRunner& runner,
                                                RawPreroutingMode raw_prerouting,
                                                bool ipv6_enabled) {
    FirewallSnapshot snapshot;
    snapshot.backend = FirewallBackend::iptables;

    const auto read_family = [&](bool ipv6) {
        const char* command = ipv6 ? "ip6tables" : "iptables";
        const auto family = ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4;
        // mangle always holds OUTPUT (and PREROUTING unless raw mode is on).
        if (!read_iptables_table(runner, command, "mangle",
                                 PhysicalTable::mangle, family,
                                 snapshot.ruleset)) {
            return false;
        }
        return !raw_prerouting.uses(ipv6) ||
               read_iptables_table(runner, command, "raw", PhysicalTable::raw,
                                   family, snapshot.ruleset);
    };

    try {
        if (!read_family(false) || (ipv6_enabled && !read_family(true))) {
            snapshot.error = "failed to inspect iptables firewall state";
            snapshot.ruleset = {};
            return snapshot;
        }
    } catch (const std::exception& error) {
        snapshot.error =
            std::string("failed to parse iptables firewall state: ") +
            error.what();
        snapshot.ruleset = {};
        return snapshot;
    }
    snapshot.available = true;
    return snapshot;
}

class IptablesSnapshotInspector final : public FirewallSnapshotInspector {
public:
    IptablesSnapshotInspector(CommandRunner runner, RawPreroutingMode mode)
        : runner_(std::move(runner)), mode_(mode) {}

    FirewallSnapshot inspect(bool ipv6_enabled) const override {
        return inspect_iptables_snapshot_impl(runner_, mode_, ipv6_enabled);
    }

private:
    CommandRunner runner_;
    RawPreroutingMode mode_;
};

class NftablesSnapshotInspector final : public FirewallSnapshotInspector {
public:
    explicit NftablesSnapshotInspector(CommandRunner runner)
        : runner_(std::move(runner)) {}

    FirewallSnapshot inspect(bool) const override {
        FirewallSnapshot snapshot;
        snapshot.backend = FirewallBackend::nftables;
        // -t (terse) leaves out set elements: only rules and chains matter
        // here and a populated static set would overflow the capture limit.
        const auto result = runner_({"nft", "-t", "-j", "list", "table", "inet",
                                     "KeenPbrTable"});
        if (read_failed(result) || result.stdout_output.empty()) {
            snapshot.error = "failed to inspect nftables firewall state";
            return snapshot;
        }
        try {
            snapshot.ruleset = parse_nft_json(result.stdout_output);
        } catch (const std::exception& error) {
            snapshot.error =
                std::string("failed to parse nftables firewall state: ") +
                error.what();
            return snapshot;
        }
        if (snapshot.ruleset.chains.empty()) {
            snapshot.error = "KeenPbrTable table not found in nftables";
            return snapshot;
        }
        snapshot.available = true;
        return snapshot;
    }

private:
    CommandRunner runner_;
};

} // namespace

FirewallSnapshot inspect_iptables_snapshot(const CommandRunner& runner,
                                           RawPreroutingMode raw_prerouting,
                                           bool ipv6_enabled) {
    return inspect_iptables_snapshot_impl(runner, raw_prerouting, ipv6_enabled);
}

FirewallSnapshot inspect_nftables_snapshot(const CommandRunner& runner) {
    return NftablesSnapshotInspector(runner).inspect(false);
}

std::unique_ptr<FirewallSnapshotInspector> create_firewall_snapshot_inspector(
    FirewallBackend backend, RawPreroutingMode raw_prerouting, CommandRunner runner) {
    if (backend == FirewallBackend::iptables) {
        return std::make_unique<IptablesSnapshotInspector>(std::move(runner),
                                                            raw_prerouting);
    }
    if (raw_prerouting.ipv4 || raw_prerouting.ipv6) {
        throw FirewallError(
            "RAW PREROUTING is supported only with the iptables firewall backend");
    }
    return std::make_unique<NftablesSnapshotInspector>(std::move(runner));
}
} // namespace keen_pbr3
