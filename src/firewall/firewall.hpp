#pragma once

#include "port_spec_util.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

class ListEntryVisitor;
enum class L4Proto : uint8_t {
    Any,
    Tcp,
    Udp,
    TcpUdp,
};

inline const char* l4_proto_name(L4Proto proto) {
    switch (proto) {
    case L4Proto::Any:
        return "";
    case L4Proto::Tcp:
        return "tcp";
    case L4Proto::Udp:
        return "udp";
    case L4Proto::TcpUdp:
        return "tcp/udp";
    }
    return "";
}

// Match criteria for firewall mark/drop/pass rules.
// All fields default to empty meaning "any".
struct FirewallRuleCriteria {
    std::optional<std::string> dst_set_name; // named destination set matcher, if any
    L4Proto proto = L4Proto::Any;
    PortSpec src_port;                 // parsed source port selector
    PortSpec dst_port;                 // parsed destination port selector
    std::vector<std::string> src_addr; // CIDR list, empty = any source address
    std::vector<std::string> dst_addr; // CIDR list, empty = any destination address
    bool negate_src_port = false;      // if true, match packets NOT from src_port
    bool negate_dst_port = false;      // if true, match packets NOT to dst_port
    bool negate_src_addr = false;      // if true, match packets NOT from src_addr
    bool negate_dst_addr = false;      // if true, match packets NOT to dst_addr
    bool empty() const {
        return !dst_set_name.has_value() && proto == L4Proto::Any
            && src_port.empty() && dst_port.empty()
            && src_addr.empty() && dst_addr.empty();
    }

    bool has_rule_selector() const {
        return dst_set_name.has_value()
            || !src_port.empty() || !dst_port.empty()
            || !src_addr.empty() || !dst_addr.empty();
    }
};

using ProtoPortFilter = FirewallRuleCriteria;

struct FirewallGlobalPrefilter {
    std::optional<std::vector<std::string>> inbound_interfaces;
    bool skip_established_or_dnat{false};
    bool skip_marked_packets{false};

    bool has_inbound_interfaces() const {
        return inbound_interfaces.has_value() && !inbound_interfaces->empty();
    }

    bool empty() const {
        return !skip_established_or_dnat && !skip_marked_packets && !has_inbound_interfaces();
    }
};

class FirewallError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Concrete firewall backend selected for runtime use.
enum class FirewallBackend : uint8_t {
    iptables,
    nftables
};

// User-facing backend preference from config.
enum class FirewallBackendPreference : uint8_t {
    auto_detect,
    iptables,
    nftables
};

// How pending firewall changes should be applied.
enum class FirewallApplyMode : uint8_t {
    // Recreate backend-owned firewall state from scratch, including sets.
    Destructive,
    // Refresh chains/rules while preserving existing set contents when possible.
    PreserveSets
};

// Abstract firewall interface for managing IP sets and packet marking rules.
// Both iptables and nftables backends implement this interface.
//
// Usage pattern (transactional rebuild):
//   create_ipset() / create_mark_rule() / create_drop_rule() / create_pass_rule() — buffer operations
//   create_batch_loader() → stream entries → finish()
//   apply()    — atomically commit everything using the requested apply mode
class Firewall {
public:
    virtual ~Firewall() = default;

    // Create a named IP set for storing IP addresses and/or CIDR subnets.
    // set_name: unique name for the set
    // family: AF_INET or AF_INET6
    // timeout: TTL in seconds for entries (0 = no timeout)
    virtual void create_ipset(const std::string& set_name, int family,
                              uint32_t timeout = 0) = 0;

    // Create a firewall rule that marks packets matching the given criteria
    // with the specified firewall mark (fwmark).
    // fwmark: mark value to apply to matching packets
    // criteria: optional match criteria (default = any packet)
    virtual void create_mark_rule(uint32_t fwmark,
                                  const FirewallRuleCriteria& criteria = {}) = 0;

    // Create a firewall rule that drops packets matching the given criteria.
    // Used for blackhole outbounds that don't need routing tables or fwmarks.
    virtual void create_drop_rule(const FirewallRuleCriteria& criteria = {}) = 0;

    // Create a firewall rule that stops keen-pbr processing for matching packets
    // and leaves them unmodified for normal system routing.
    virtual void create_pass_rule(const FirewallRuleCriteria& criteria = {}) = 0;

    // Create a batch loader visitor for streaming IP/CIDR entries into a set.
    // Returns a ListEntryVisitor that buffers entries for atomic application.
    // Caller must call finish() on the returned visitor after streaming is complete.
    virtual std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name) = 0;

    // Apply all pending changes atomically (where supported by the backend).
    virtual void apply(FirewallApplyMode mode = FirewallApplyMode::Destructive) = 0;

    // Configure a backend-wide prefilter emitted ahead of mark/drop/pass rules.
    void set_global_prefilter(FirewallGlobalPrefilter prefilter) {
        global_prefilter_ = std::move(prefilter);
    }

    const FirewallGlobalPrefilter& global_prefilter() const {
        return global_prefilter_;
    }

    void set_fwmark_mask(uint32_t fwmark_mask) {
        fwmark_mask_ = fwmark_mask;
    }

    uint32_t fwmark_mask() const {
        return fwmark_mask_;
    }

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

    FirewallGlobalPrefilter global_prefilter_;
    uint32_t fwmark_mask_{0xFFFFFFFFu};
};

// Detect which firewall backend is available on the system.
// Checks for nftables first (modern), falls back to iptables.
// Throws FirewallError if neither is available.
FirewallBackend detect_firewall_backend();

// Return the stable config/CLI label for a concrete backend.
const char* firewall_backend_name(FirewallBackend backend);

// Factory function to create the appropriate firewall backend.
// backend_pref: auto-detect, iptables, or nftables.
// Throws FirewallError if requested backend is not available.
std::unique_ptr<Firewall> create_firewall(
    FirewallBackendPreference backend_pref = FirewallBackendPreference::auto_detect);

} // namespace keen_pbr3
