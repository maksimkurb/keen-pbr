#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

class ListEntryVisitor;

class FirewallError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Abstract firewall interface for managing IP sets and packet marking rules.
// Both iptables and nftables backends implement this interface.
class Firewall {
public:
    virtual ~Firewall() = default;

    // Create a named IP set for storing IP addresses and/or CIDR subnets.
    // set_name: unique name for the set
    // family: AF_INET or AF_INET6
    // timeout: TTL in seconds for entries (0 = no timeout)
    virtual void create_ipset(const std::string& set_name, int family,
                              uint32_t timeout = 0) = 0;

    // Add an IP address or CIDR subnet to a named set.
    // entry: IP address (e.g., "1.2.3.4") or CIDR (e.g., "10.0.0.0/8")
    // entry_timeout: per-entry timeout in seconds (-1 = use set default, 0 = permanent)
    virtual void add_to_ipset(const std::string& set_name, const std::string& entry,
                              int32_t entry_timeout = -1) = 0;

    // Delete a named IP set and all its entries.
    virtual void delete_ipset(const std::string& set_name) = 0;

    // Create a firewall rule that marks packets matching the given IP set
    // with the specified firewall mark (fwmark).
    // set_name: IP set to match against
    // fwmark: mark value to apply to matching packets
    // chain: iptables chain / nft chain to insert the rule
    virtual void create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                  const std::string& chain = "PREROUTING") = 0;

    // Delete a mark rule previously created with create_mark_rule.
    virtual void delete_mark_rule(const std::string& set_name, uint32_t fwmark,
                                  const std::string& chain = "PREROUTING") = 0;

    // Create a firewall rule that drops packets matching the given IP set.
    // Used for blackhole outbounds that don't need routing tables or fwmarks.
    // set_name: IP set to match against
    // chain: iptables chain / nft chain to insert the rule
    virtual void create_drop_rule(const std::string& set_name,
                                  const std::string& chain = "PREROUTING") = 0;

    // Create a batch loader visitor for streaming IP/CIDR entries into a set.
    // Returns a ListEntryVisitor that pipes entries to the backend's batch command.
    // entry_timeout: per-entry timeout in seconds (-1 = use set default, 0 = permanent)
    // Caller must call finish() on the returned visitor after streaming is complete.
    virtual std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name, int32_t entry_timeout = -1) = 0;

    // Flush all entries from a named IP set without destroying the set itself.
    virtual void flush_ipset(const std::string& set_name) = 0;

    // Apply all pending changes atomically (where supported by the backend).
    virtual void apply() = 0;

    // Remove all firewall rules and IP sets created by this instance.
    // Should be called on daemon shutdown.
    virtual void cleanup() = 0;

    // Non-copyable
    Firewall(const Firewall&) = delete;
    Firewall& operator=(const Firewall&) = delete;

protected:
    Firewall() = default;
};

// Firewall backend type
enum class FirewallBackend {
    iptables,
    nftables
};

// Detect which firewall backend is available on the system.
// Checks for nftables first (modern), falls back to iptables.
// Throws FirewallError if neither is available.
FirewallBackend detect_firewall_backend();

// Factory function to create the appropriate firewall backend.
// backend_pref: "auto" (detect), "iptables", or "nftables"
// Throws FirewallError if requested backend is not available.
std::unique_ptr<Firewall> create_firewall(const std::string& backend_pref = "auto");

} // namespace keen_pbr3
