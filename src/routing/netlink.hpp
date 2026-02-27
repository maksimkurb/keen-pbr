#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

// A route dumped from the kernel (read-only snapshot)
struct DumpedRoute {
    std::string destination;            // "default" for 0/0, else "x.x.x.x/N"
    uint32_t table{0};                  // Routing table ID
    std::optional<std::string> interface;  // Output interface name (if any)
    std::optional<std::string> gateway;    // Next-hop gateway IP (if any)
    bool blackhole{false};              // True if route type is RTN_BLACKHOLE
    int family{0};                      // AF_INET or AF_INET6
};

// A policy rule dumped from the kernel (read-only snapshot)
struct DumpedRule {
    uint32_t priority{0};
    uint32_t fwmark{0};
    uint32_t fwmask{0};
    uint32_t table{0};
    int family{0};              // AF_INET or AF_INET6
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

    // Dump all routes in a specific routing table from the kernel.
    // family: 0 (AF_UNSPEC) to get both IPv4 and IPv6 routes.
    std::vector<DumpedRoute> dump_routes_in_table(uint32_t table_id,
                                                   int family = 0);

    // Dump all policy rules from the kernel.
    // family: 0 (AF_UNSPEC) to get both IPv4 and IPv6 rules.
    std::vector<DumpedRule> dump_policy_rules(int family = 0);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace keen_pbr3
