#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

class ListEntryVisitor;

// Protocol + port + address filter for firewall mark/drop rules.
// All fields default to empty meaning "any".
struct ProtoPortFilter {
    std::string proto;                  // "tcp", "udp", "tcp/udp", or "" (any)
    std::string src_port;              // port spec or "" (any)
    std::string dst_port;              // port spec or "" (any)
    std::vector<std::string> src_addr; // CIDR list, empty = any source address
    std::vector<std::string> dst_addr; // CIDR list, empty = any destination address
    bool negate_src_port = false;      // if true, match packets NOT from src_port
    bool negate_dst_port = false;      // if true, match packets NOT to dst_port
    bool negate_src_addr = false;      // if true, match packets NOT from src_addr
    bool negate_dst_addr = false;      // if true, match packets NOT to dst_addr
    bool empty() const {
        return proto.empty() && src_port.empty() && dst_port.empty()
            && src_addr.empty() && dst_addr.empty();
    }
};

class FirewallError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Firewall backend type
enum class FirewallBackend {
    iptables,
    nftables
};

// Abstract firewall interface for managing IP sets and packet marking rules.
// Both iptables and nftables backends implement this interface.
//
// Usage pattern (transactional rebuild):
//   cleanup()  — remove all previous state
//   create_ipset() / create_mark_rule() / create_drop_rule() — buffer operations
//   create_batch_loader() → stream entries → finish()
//   apply()    — atomically commit everything
class Firewall {
public:
    virtual ~Firewall() = default;

    // Create a named IP set for storing IP addresses and/or CIDR subnets.
    // set_name: unique name for the set
    // family: AF_INET or AF_INET6
    // timeout: TTL in seconds for entries (0 = no timeout)
    virtual void create_ipset(const std::string& set_name, int family,
                              uint32_t timeout = 0) = 0;

    // Create a firewall rule that marks packets matching the given IP set
    // with the specified firewall mark (fwmark).
    // set_name: IP set to match against
    // fwmark: mark value to apply to matching packets
    // filter: optional proto/port filter (default = any proto, any port)
    virtual void create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                  const ProtoPortFilter& filter = {}) = 0;

    // Create a firewall rule that drops packets matching the given IP set.
    // Used for blackhole outbounds that don't need routing tables or fwmarks.
    // set_name: IP set to match against
    // filter: optional proto/port filter (default = any proto, any port)
    virtual void create_drop_rule(const std::string& set_name,
                                  const ProtoPortFilter& filter = {}) = 0;

    // Create a firewall rule that marks packets matching the filter's dst_addr
    // with the specified fwmark, WITHOUT requiring an IP set.
    // Used for DNS server detour (single IP + port 53).
    // When filter is empty (all fields default), acts as a catch-all mark rule.
    virtual void create_direct_mark_rule(uint32_t fwmark,
                                         const ProtoPortFilter& filter) = 0;

    // Create a batch loader visitor for streaming IP/CIDR entries into a set.
    // Returns a ListEntryVisitor that buffers entries for atomic application.
    // entry_timeout: per-entry timeout in seconds (-1 = use set default, 0 = permanent)
    // Caller must call finish() on the returned visitor after streaming is complete.
    virtual std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name, int32_t entry_timeout = -1) = 0;

    // Apply all pending changes atomically (where supported by the backend).
    virtual void apply() = 0;

    // Remove all firewall rules and IP sets created by this instance.
    // Should be called on daemon shutdown.
    virtual void cleanup() = 0;

    // Return the backend type for this firewall instance.
    virtual FirewallBackend backend() const = 0;

    // Test whether an IP address is present in a named set on the live system.
    // Returns true  if the IP is in the set.
    // Returns false if the IP is not in the set or the set does not exist.
    // Returns nullopt if the underlying tool is unavailable (result is unknown).
    virtual std::optional<bool> test_ip_in_set(const std::string& set_name,
                                                const std::string& ip) const = 0;

    // Non-copyable
    Firewall(const Firewall&) = delete;
    Firewall& operator=(const Firewall&) = delete;

protected:
    Firewall() = default;
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
