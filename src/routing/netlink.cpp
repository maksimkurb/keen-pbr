#include "netlink.hpp"

#include "../util/format_compat.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <memory>
#include <net/if.h>
#include <netinet/in.h>

#include <netlink/cache.h>
#include <netlink/errno.h>
#include <netlink/netlink.h>
#include <netlink/route/route.h>
#include <netlink/route/rule.h>
#include <netlink/route/nexthop.h>

namespace keen_pbr3 {

namespace {

// Convert an nl_addr to a plain IP string (no prefix length suffix).
std::string nl_addr_to_ip_str(struct nl_addr* addr) {
    if (!addr) return "";
    char buf[128];
    nl_addr2str(addr, buf, sizeof(buf));
    std::string s(buf);
    auto pos = s.find('/');
    if (pos != std::string::npos) {
        s = s.substr(0, pos);
    }
    return s;
}

// RAII wrapper for nl_cache
struct CacheDeleter {
    void operator()(struct nl_cache* c) const {
        if (c) nl_cache_free(c);
    }
};
using CachePtr = std::unique_ptr<struct nl_cache, CacheDeleter>;

// Parse an IP address string to nl_addr, auto-detecting family.
// For CIDR notation (e.g., "10.0.0.0/8"), the prefix length is preserved.
// For plain IPs, uses host prefix length (32 for v4, 128 for v6).
struct NlAddrDeleter {
    void operator()(struct nl_addr* a) const {
        if (a) nl_addr_put(a);
    }
};
using NlAddrPtr = std::unique_ptr<struct nl_addr, NlAddrDeleter>;

NlAddrPtr parse_addr(const std::string& addr_str, int hint_family = AF_UNSPEC) {
    struct nl_addr* addr = nullptr;
    int err = nl_addr_parse(addr_str.c_str(), hint_family, &addr);
    if (err < 0) {
        throw NetlinkError("Failed to parse address '" + addr_str + "': " +
                           nl_geterror(err));
    }
    return NlAddrPtr(addr);
}

int detect_family(const std::string& addr_str) {
    if (addr_str.find(':') != std::string::npos) {
        return AF_INET6;
    }
    return AF_INET;
}

// RAII wrapper for rtnl_route
struct RouteDeleter {
    void operator()(struct rtnl_route* r) const {
        if (r) rtnl_route_put(r);
    }
};
using RoutePtr = std::unique_ptr<struct rtnl_route, RouteDeleter>;

// RAII wrapper for rtnl_nexthop
struct NexthopDeleter {
    void operator()(struct rtnl_nexthop* nh) const {
        if (nh) rtnl_route_nh_free(nh);
    }
};
using NexthopPtr = std::unique_ptr<struct rtnl_nexthop, NexthopDeleter>;

// RAII wrapper for rtnl_rule
struct RuleDeleter {
    void operator()(struct rtnl_rule* r) const {
        if (r) rtnl_rule_put(r);
    }
};
using RulePtr = std::unique_ptr<struct rtnl_rule, RuleDeleter>;

} // anonymous namespace

struct NetlinkManager::Impl {
    struct nl_sock* sock{nullptr};

    Impl() {
        sock = nl_socket_alloc();
        if (!sock) {
            throw NetlinkError("Failed to allocate netlink socket");
        }
        int err = nl_connect(sock, NETLINK_ROUTE);
        if (err < 0) {
#ifdef KEEN_PBR3_TESTING
            nl_socket_free(sock);
            sock = nullptr;
            return;
#else
            nl_socket_free(sock);
            throw NetlinkError(std::string("Failed to connect netlink socket: ") +
                               nl_geterror(err));
#endif
        }
    }

    ~Impl() {
        if (sock) {
            nl_close(sock);
            nl_socket_free(sock);
        }
    }
};

NetlinkManager::NetlinkManager() : impl_(new Impl()) {}

NetlinkManager::~NetlinkManager() {
    delete impl_;
}

void NetlinkManager::add_route(const RouteSpec& spec) {
    int family = spec.family;
    if (family == 0) {
        if (spec.destination == "default") {
            family = AF_INET;
        } else {
            family = detect_family(spec.destination);
        }
    }

    RoutePtr route(rtnl_route_alloc());
    if (!route) {
        throw NetlinkError("Failed to allocate route object");
    }

    rtnl_route_set_family(route.get(), family);

    if (spec.table != 0) {
        rtnl_route_set_table(route.get(), spec.table);
    }

    if (spec.metric != 0) {
        rtnl_route_set_priority(route.get(), spec.metric);
    }

    // Set destination
    if (spec.destination == "default") {
        // Default route: destination is 0.0.0.0/0 or ::/0
        NlAddrPtr dst = parse_addr(family == AF_INET6 ? "::/0" : "0.0.0.0/0", family);
        rtnl_route_set_dst(route.get(), dst.get());
    } else {
        NlAddrPtr dst = parse_addr(spec.destination, family);
        rtnl_route_set_dst(route.get(), dst.get());
    }

    if (spec.blackhole) {
        rtnl_route_set_type(route.get(), RTN_BLACKHOLE);
    } else if (spec.unreachable) {
        rtnl_route_set_type(route.get(), RTN_UNREACHABLE);
    } else {
        // Create nexthop with interface and optional gateway
        NexthopPtr nh(rtnl_route_nh_alloc());
        if (!nh) {
            throw NetlinkError("Failed to allocate nexthop object");
        }

        if (spec.interface) {
            unsigned int ifindex = if_nametoindex(spec.interface->c_str());
            if (ifindex == 0) {
                throw NetlinkError("Interface not found: " + *spec.interface);
            }
            rtnl_route_nh_set_ifindex(nh.get(), static_cast<int>(ifindex));
        }

        if (spec.gateway) {
            NlAddrPtr gw = parse_addr(*spec.gateway, family);
            rtnl_route_nh_set_gateway(nh.get(), gw.get());
        }

        // rtnl_route_add_nexthop takes ownership of nh
        rtnl_route_add_nexthop(route.get(), nh.release());
    }

    int err = rtnl_route_add(impl_->sock, route.get(), NLM_F_CREATE | NLM_F_REPLACE);
    if (err < 0) {
        throw NetlinkError(keen_pbr3::format(
            "Failed to add route: {} (dst={}, table={}, iface={}, gw={}, family={}, blackhole={})",
            nl_geterror(err),
            spec.destination,
            spec.table,
            spec.interface.value_or("(none)"),
            spec.gateway.value_or("(none)"),
            family,
            spec.blackhole));
    }
}

void NetlinkManager::delete_route(const RouteSpec& spec) {
    int family = spec.family;
    if (family == 0) {
        if (spec.destination == "default") {
            family = AF_INET;
        } else {
            family = detect_family(spec.destination);
        }
    }

    RoutePtr route(rtnl_route_alloc());
    if (!route) {
        throw NetlinkError("Failed to allocate route object");
    }

    rtnl_route_set_family(route.get(), family);

    if (spec.table != 0) {
        rtnl_route_set_table(route.get(), spec.table);
    }

    if (spec.metric != 0) {
        rtnl_route_set_priority(route.get(), spec.metric);
    }

    if (spec.destination == "default") {
        NlAddrPtr dst = parse_addr(family == AF_INET6 ? "::/0" : "0.0.0.0/0", family);
        rtnl_route_set_dst(route.get(), dst.get());
    } else {
        NlAddrPtr dst = parse_addr(spec.destination, family);
        rtnl_route_set_dst(route.get(), dst.get());
    }

    if (spec.blackhole) {
        rtnl_route_set_type(route.get(), RTN_BLACKHOLE);
    } else if (spec.unreachable) {
        rtnl_route_set_type(route.get(), RTN_UNREACHABLE);
    } else if (spec.interface) {
        NexthopPtr nh(rtnl_route_nh_alloc());
        if (!nh) {
            throw NetlinkError("Failed to allocate nexthop object");
        }
        unsigned int ifindex = if_nametoindex(spec.interface->c_str());
        if (ifindex == 0) {
            throw NetlinkError("Interface not found: " + *spec.interface);
        }
        rtnl_route_nh_set_ifindex(nh.get(), static_cast<int>(ifindex));

        if (spec.gateway) {
            NlAddrPtr gw = parse_addr(*spec.gateway, family);
            rtnl_route_nh_set_gateway(nh.get(), gw.get());
        }

        rtnl_route_add_nexthop(route.get(), nh.release());
    }

    int err = rtnl_route_delete(impl_->sock, route.get(), 0);
    if (err < 0) {
        throw NetlinkError(std::string("Failed to delete route: ") + nl_geterror(err));
    }
}

void NetlinkManager::flush_routes_in_table(uint32_t table_id, int family) {
    struct nl_cache* raw_cache = nullptr;
    int err = rtnl_route_alloc_cache(impl_->sock, family, 0, &raw_cache);
    if (err < 0) {
        throw NetlinkError(std::string("Failed to alloc route cache: ") +
                           nl_geterror(err));
    }
    CachePtr cache(raw_cache);

    std::vector<RoutePtr> routes_to_delete;
    nl_cache_foreach(cache.get(), [](struct nl_object* obj, void* arg) {
        auto* routes = static_cast<std::vector<RoutePtr>*>(arg);
        auto* route = reinterpret_cast<struct rtnl_route*>(obj);
        if (rtnl_route_get_table(route) == 0) {
            return;
        }
        rtnl_route_get(route);
        routes->emplace_back(route);
    }, &routes_to_delete);

    for (auto& route : routes_to_delete) {
        if (rtnl_route_get_table(route.get()) != table_id) {
            continue;
        }
        try {
            int delete_err = rtnl_route_delete(impl_->sock, route.get(), 0);
            if (delete_err < 0) {
                throw NetlinkError(std::string("Failed to delete route during flush: ") +
                                   nl_geterror(delete_err));
            }
        } catch (...) {
            // Best effort: continue flushing remaining routes in this table.
        }
    }
}

void NetlinkManager::add_rule(const RuleSpec& spec) {
    auto add_for_family = [&](int fam) {
        RulePtr rule(rtnl_rule_alloc());
        if (!rule) {
            throw NetlinkError("Failed to allocate rule object");
        }

        rtnl_rule_set_family(rule.get(), fam);
        rtnl_rule_set_table(rule.get(), spec.table);
        rtnl_rule_set_mark(rule.get(), spec.fwmark);
        rtnl_rule_set_mask(rule.get(), spec.fwmask);

        if (spec.priority != 0) {
            rtnl_rule_set_prio(rule.get(), spec.priority);
        }

        rtnl_rule_set_action(rule.get(), FR_ACT_TO_TBL);

        int err = rtnl_rule_add(impl_->sock, rule.get(), NLM_F_CREATE | NLM_F_EXCL);
        if (err < 0) {
            if (err == -NLE_EXIST) {
                // Rule already exists (e.g., leftover from a crashed previous instance).
                // Desired state is already achieved — not an error.
                return;
            }
            throw NetlinkError(std::string("Failed to add rule (family ") +
                               std::to_string(fam) + "): " + nl_geterror(err));
        }
    };

    if (spec.family == 0) {
        // Add for both IPv4 and IPv6
        add_for_family(AF_INET);
        add_for_family(AF_INET6);
    } else {
        add_for_family(spec.family);
    }
}

void NetlinkManager::delete_rule(const RuleSpec& spec) {
    auto del_for_family = [&](int fam) {
        RulePtr rule(rtnl_rule_alloc());
        if (!rule) {
            throw NetlinkError("Failed to allocate rule object");
        }

        rtnl_rule_set_family(rule.get(), fam);
        rtnl_rule_set_table(rule.get(), spec.table);
        rtnl_rule_set_mark(rule.get(), spec.fwmark);
        rtnl_rule_set_mask(rule.get(), spec.fwmask);

        if (spec.priority != 0) {
            rtnl_rule_set_prio(rule.get(), spec.priority);
        }

        rtnl_rule_set_action(rule.get(), FR_ACT_TO_TBL);

        int err = rtnl_rule_delete(impl_->sock, rule.get(), 0);
        if (err < 0) {
            throw NetlinkError(std::string("Failed to delete rule (family ") +
                               std::to_string(fam) + "): " + nl_geterror(err));
        }
    };

    if (spec.family == 0) {
        del_for_family(AF_INET);
        del_for_family(AF_INET6);
    } else {
        del_for_family(spec.family);
    }
}

std::vector<DumpedRoute> NetlinkManager::dump_routes_in_table(uint32_t table_id,
                                                              int family) {
    struct nl_cache* raw_cache = nullptr;
    int err = rtnl_route_alloc_cache(impl_->sock, family, 0, &raw_cache);
    if (err < 0) {
        throw NetlinkError(std::string("Failed to alloc route cache: ") +
                           nl_geterror(err));
    }
    CachePtr cache(raw_cache);

    std::vector<DumpedRoute> result;
    struct DumpRoutesCtx {
        std::vector<DumpedRoute>* result;
        uint32_t table_id;
    } ctx{&result, table_id};

    nl_cache_foreach(cache.get(), [](struct nl_object* obj, void* arg) {
        auto* ctx = static_cast<DumpRoutesCtx*>(arg);
        auto* route = reinterpret_cast<struct rtnl_route*>(obj);

        // Filter by table
        if (rtnl_route_get_table(route) != ctx->table_id) {
            return;
        }

        DumpedRoute dr;
        dr.table = ctx->table_id;
        dr.family = rtnl_route_get_family(route);
        dr.metric = static_cast<uint32_t>(rtnl_route_get_priority(route));

        // Determine route type
        int rt_type = rtnl_route_get_type(route);
        dr.blackhole = (rt_type == RTN_BLACKHOLE);
        dr.unreachable = (rt_type == RTN_UNREACHABLE);

        // Destination
        struct nl_addr* dst = rtnl_route_get_dst(route);
        if (dst) {
            int prefixlen = nl_addr_get_prefixlen(dst);
            if (prefixlen == 0) {
                dr.destination = "default";
            } else {
                char buf[128];
                nl_addr2str(dst, buf, sizeof(buf));
                dr.destination = buf;
            }
        }

        // Nexthop info (interface and gateway)
        if (!dr.blackhole && !dr.unreachable) {
            int nh_count = rtnl_route_get_nnexthops(route);
            if (nh_count > 0) {
                struct rtnl_nexthop* nh = rtnl_route_nexthop_n(route, 0);
                if (nh) {
                    int ifindex = rtnl_route_nh_get_ifindex(nh);
                    if (ifindex > 0) {
                        char ifname[IF_NAMESIZE];
                        if (if_indextoname(static_cast<unsigned>(ifindex),
                                           ifname)) {
                            dr.interface = ifname;
                        }
                    }
                    struct nl_addr* gw = rtnl_route_nh_get_gateway(nh);
                    if (gw && nl_addr_get_len(gw) > 0) {
                        dr.gateway = nl_addr_to_ip_str(gw);
                    }
                }
            }
        }

        ctx->result->push_back(std::move(dr));
    }, &ctx);

    return result;
}

std::vector<DumpedRule> NetlinkManager::dump_policy_rules(int family) {
    struct nl_cache* raw_cache = nullptr;
    int err = rtnl_rule_alloc_cache(impl_->sock, family, &raw_cache);
    if (err < 0) {
        throw NetlinkError(std::string("Failed to alloc rule cache: ") +
                           nl_geterror(err));
    }
    CachePtr cache(raw_cache);

    std::vector<DumpedRule> result;

    nl_cache_foreach(cache.get(), [](struct nl_object* obj, void* arg) {
        auto* out = static_cast<std::vector<DumpedRule>*>(arg);
        auto* rule = reinterpret_cast<struct rtnl_rule*>(obj);

        DumpedRule dr;
        dr.priority = rtnl_rule_get_prio(rule);
        dr.fwmark   = rtnl_rule_get_mark(rule);
        dr.fwmask   = rtnl_rule_get_mask(rule);
        dr.table    = rtnl_rule_get_table(rule);
        dr.family   = rtnl_rule_get_family(rule);

        out->push_back(dr);
    }, &result);

    return result;
}

} // namespace keen_pbr3
