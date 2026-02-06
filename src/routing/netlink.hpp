#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

class NetlinkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Represents a route to install in the kernel
struct RouteSpec {
    std::string destination;    // IP/CIDR (e.g., "10.0.0.0/8") or "default"
    uint32_t table{0};          // Routing table ID (0 = main)
    std::optional<std::string> interface;   // Output interface name
    std::optional<std::string> gateway;     // Next-hop gateway IP
    bool blackhole{false};      // If true, create a blackhole route
    int family{0};              // AF_INET or AF_INET6 (0 = auto-detect)
};

// Represents a policy routing rule (ip rule)
struct RuleSpec {
    uint32_t fwmark{0};         // Firewall mark to match
    uint32_t fwmask{0xFFFFFFFF}; // Firewall mark mask
    uint32_t table{0};          // Routing table to use
    uint32_t priority{0};       // Rule priority (lower = higher priority)
    int family{0};              // AF_INET or AF_INET6 (0 = both)
};

// Low-level netlink route and policy rule management via libnl
class NetlinkManager {
public:
    NetlinkManager();
    ~NetlinkManager();

    // Non-copyable, non-movable (owns netlink socket)
    NetlinkManager(const NetlinkManager&) = delete;
    NetlinkManager& operator=(const NetlinkManager&) = delete;
    NetlinkManager(NetlinkManager&&) = delete;
    NetlinkManager& operator=(NetlinkManager&&) = delete;

    // Route operations
    void add_route(const RouteSpec& spec);
    void delete_route(const RouteSpec& spec);

    // Policy rule operations
    void add_rule(const RuleSpec& spec);
    void delete_rule(const RuleSpec& spec);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace keen_pbr3
