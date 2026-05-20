#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace keen_pbr3 {

// keen-pbr routes domain-based traffic by letting dnsmasq add resolved IPs to
// "dynamic" ipsets/nft sets (kpbr4d_* / kpbr6d_*). The firewall marks packets
// whose destination is in such a set, and the mark selects a routing table.
//
// The mark is evaluated on the first packet of a flow; once conntrack has
// confirmed the flow it keeps following the original (unmarked) path. So a
// connection that was established *before* a domain's IP entered the set --
// or that raced the set update -- keeps bypassing policy routing for its whole
// lifetime. This is the classic "the route is configured but traffic still
// goes out the wrong interface" symptom.
//
// ConntrackRerouter watches the dynamic sets and, when a new IP appears,
// flushes the stale conntrack entries for it so the next packet re-traverses
// the mangle table and picks up the correct mark/route.
//
// The class itself is pure logic: I/O (reading sets, flushing conntrack) is
// injected, which keeps it unit-testable. Default production implementations
// are provided as free functions below.

// Reads the current members of the named dynamic routing sets.
// Returns set-name -> member IPs. Sets that do not exist are simply omitted.
using DynamicSetReader = std::function<
    std::map<std::string, std::vector<std::string>>(const std::vector<std::string>&)>;

// Flushes conntrack entries whose original-direction destination is `ip`.
using ConntrackFlusher = std::function<void(const std::string& ip)>;

class ConntrackRerouter {
public:
    ConntrackRerouter(DynamicSetReader reader, ConntrackFlusher flusher);

    // Run one poll cycle over the given dynamic set names. For every IP that
    // appeared since the previous poll, the conntrack flusher is invoked.
    // Returns the number of IPs flushed.
    //
    // The first time a set is observed its members are recorded as a silent
    // baseline (nothing is flushed) so that startup -- when every set looks
    // "new" -- does not trigger a flush storm.
    std::size_t poll(const std::vector<std::string>& set_names);

    // Forget every baseline. The next poll re-baselines all sets. Call this
    // after a config reload, when the firewall (and its sets) was rebuilt.
    void reset();

private:
    DynamicSetReader reader_;
    ConntrackFlusher flusher_;
    std::map<std::string, std::set<std::string>> known_members_;
};

// Production DynamicSetReader for the iptables/ipset backend (parses
// `ipset save <name>` for each set).
std::map<std::string, std::vector<std::string>>
read_ipset_dynamic_sets(const std::vector<std::string>& set_names);

// Production DynamicSetReader for the nftables backend (parses
// `nft -j list set inet KeenPbrTable <name>` for each set).
std::map<std::string, std::vector<std::string>>
read_nft_dynamic_sets(const std::vector<std::string>& set_names);

// Production ConntrackFlusher: invokes the `conntrack` tool. The tool is
// probed once on first use; if it is unavailable a single warning is logged
// and all further calls become no-ops.
void flush_conntrack_for_ip(const std::string& ip);

} // namespace keen_pbr3
