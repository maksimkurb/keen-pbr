#include "iptables.hpp"
#include "ipset_restore_pipe.hpp"
#include "port_spec_util.hpp"
#include "../log/logger.hpp"
#include "../util/format_compat.hpp"
#include "../util/safe_exec.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>

namespace keen_pbr3 {

IptablesFirewall::IptablesFirewall() = default;

IptablesFirewall::~IptablesFirewall() {
    try {
        cleanup();
    } catch (...) {
        // Best-effort cleanup in destructor
    }
}

void IptablesFirewall::create_ipset(const std::string& set_name, int family,
                                     uint32_t timeout) {
    PendingSet ps;
    ps.name = set_name;
    ps.family_str = (family == AF_INET6) ? "inet6" : "inet";
    ps.timeout = timeout;
    pending_sets_.push_back(std::move(ps));
    created_sets_[set_name] = family;
}

void IptablesFirewall::expand_and_push(std::vector<PendingRule>& out,
                                        const std::string& set_name, bool ipv6,
                                        PendingRule::Action action, uint32_t fwmark,
                                        const ProtoPortFilter& filter) {
    std::vector<std::string> protos = (filter.proto == "tcp/udp")
        ? std::vector<std::string>{"tcp", "udp"}
        : std::vector<std::string>{filter.proto};
    const std::vector<std::string> any_addr{""};
    const std::vector<std::string>& src_addrs =
        filter.src_addr.empty() ? any_addr : filter.src_addr;
    const std::vector<std::string>& dst_addrs =
        filter.dst_addr.empty() ? any_addr : filter.dst_addr;

    for (const auto& proto : protos) {
        for (const auto& src : src_addrs) {
            for (const auto& dst : dst_addrs) {
                PendingRule pr;
                pr.set_name = set_name;
                pr.ipv6     = ipv6;
                pr.action   = action;
                pr.fwmark   = fwmark;
                pr.filter   = filter;
                pr.filter.proto = proto;
                pr.filter.src_addr = src.empty() ? std::vector<std::string>{} : std::vector<std::string>{src};
                pr.filter.dst_addr = dst.empty() ? std::vector<std::string>{} : std::vector<std::string>{dst};
                out.push_back(std::move(pr));
            }
        }
    }
}

void IptablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const ProtoPortFilter& filter) {
    auto it = created_sets_.find(set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
    expand_and_push(pending_rules_, set_name, ipv6, PendingRule::Mark, fwmark, filter);
}

void IptablesFirewall::create_drop_rule(const std::string& set_name,
                                         const ProtoPortFilter& filter) {
    auto it = created_sets_.find(set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
    expand_and_push(pending_rules_, set_name, ipv6, PendingRule::Drop, 0, filter);
}

void IptablesFirewall::create_pass_rule(const std::string& set_name,
                                         const ProtoPortFilter& filter) {
    auto it = created_sets_.find(set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
    expand_and_push(pending_rules_, set_name, ipv6, PendingRule::Pass, 0, filter);
}

void IptablesFirewall::create_direct_mark_rule(uint32_t fwmark,
                                                const ProtoPortFilter& filter) {
    bool is_v6 = !filter.dst_addr.empty() &&
                 filter.dst_addr[0].find(':') != std::string::npos;
    std::vector<std::string> protos = (filter.proto == "tcp/udp")
        ? std::vector<std::string>{"tcp", "udp"}
        : std::vector<std::string>{filter.proto};
    for (const auto& proto : protos) {
        PendingRule pr;
        pr.direct  = true;
        pr.ipv6    = is_v6;
        pr.action  = PendingRule::Mark;
        pr.fwmark  = fwmark;
        pr.filter  = filter;
        pr.filter.proto = proto;
        pending_rules_.push_back(std::move(pr));
    }
}

std::unique_ptr<ListEntryVisitor> IptablesFirewall::create_batch_loader(
    const std::string& set_name) {
    auto& buf = pending_elements_[set_name];
    return std::make_unique<IpsetRestoreVisitor>(buf, set_name);
}

static void pipe_to_cmd(const std::vector<std::string>& args, const std::string& input) {
    Logger::instance().verbose("{} script:\n{}", args[0], input);
    int status = safe_exec_pipe_stdin(args, input);
    if (status != 0) {
        throw FirewallError(keen_pbr3::format("{} exited with status {}", args[0], status));
    }
}

std::string IptablesFirewall::build_ipset_create_line(const PendingSet& ps) {
    if (ps.timeout > 0) {
        return keen_pbr3::format("create {} hash:net family {} timeout {} -exist\n",
                                 ps.name, ps.family_str, ps.timeout);
    } else {
        return keen_pbr3::format("create {} hash:net family {} -exist\n",
                                 ps.name, ps.family_str);
    }
}

std::string IptablesFirewall::build_proto_port_fragment(const std::string& proto,
                                                         const std::string& src_port,
                                                         const std::string& dst_port,
                                                         bool negate_src_port,
                                                         bool negate_dst_port) {
    if (proto.empty() && src_port.empty() && dst_port.empty()) {
        return "";
    }

    std::string frag;

    // -p flag (required if we have any port matching)
    if (!proto.empty()) {
        frag += " -p " + proto;
    }

    bool has_src = !src_port.empty();
    bool has_dst = !dst_port.empty();
    bool src_list = has_src && classify_port_spec(src_port) == PortSpecKind::List;
    bool dst_list = has_dst && classify_port_spec(dst_port) == PortSpecKind::List;
    std::string normalized_src = has_src ? normalize_port_spec_for_iptables(src_port) : "";
    std::string normalized_dst = has_dst ? normalize_port_spec_for_iptables(dst_port) : "";

    // Use -m multiport when: both ports present, or either is a comma list
    if (has_src || has_dst) {
        if (src_list || dst_list || (has_src && has_dst)) {
            // When negation differs between src and dst, emit separate -m multiport clauses
            // (iptables cannot mix negated and non-negated flags in one multiport call).
            if (has_src && has_dst && negate_src_port != negate_dst_port) {
                frag += " -m multiport" + std::string(negate_src_port ? " !" : "") + " --sports " + normalized_src;
                frag += " -m multiport" + std::string(negate_dst_port ? " !" : "") + " --dports " + normalized_dst;
            } else {
                frag += " -m multiport";
                if (has_src) frag += std::string(negate_src_port ? " !" : "") + " --sports " + normalized_src;
                if (has_dst) frag += std::string(negate_dst_port ? " !" : "") + " --dports " + normalized_dst;
            }
        } else {
            // Single port or range, single direction — use --sport/--dport
            if (has_src) frag += std::string(negate_src_port ? " !" : "") + " --sport " + normalized_src;
            if (has_dst) frag += std::string(negate_dst_port ? " !" : "") + " --dport " + normalized_dst;
        }
    }

    return frag;
}

std::string IptablesFirewall::build_prefilter_lines(
    const FirewallGlobalPrefilter& prefilter) {
    std::string lines;
    if (prefilter.skip_established_or_dnat) {
        lines += keen_pbr3::format(
            "-A {} -m conntrack --ctstatus DNAT -j RETURN\n",
            CHAIN_NAME);
    }

    if (prefilter.has_inbound_interfaces()
        && prefilter.inbound_interfaces->size() == 1) {
        lines += keen_pbr3::format(
            "-A {} ! -i {} -j RETURN\n",
            CHAIN_NAME,
            prefilter.inbound_interfaces->front());
    }

    return lines;
}

std::vector<std::string> IptablesFirewall::build_rule_lines(
    const PendingRule& pr,
    const FirewallGlobalPrefilter& prefilter) {
    // iptables cannot express a multi-value negated -i guard in one rule, so
    // multi-interface allowlists are expanded into one positive -i match per rule.
    std::vector<std::string> iface_frags;
    if (prefilter.has_inbound_interfaces()
        && prefilter.inbound_interfaces->size() > 1) {
        iface_frags.reserve(prefilter.inbound_interfaces->size());
        for (const auto& iface : *prefilter.inbound_interfaces) {
            iface_frags.push_back(" -i " + iface);
        }
    } else {
        iface_frags.push_back("");
    }

    std::string addr_frag;
    if (!pr.filter.src_addr.empty())
        addr_frag += std::string(pr.filter.negate_src_addr ? " !" : "") + " -s " + pr.filter.src_addr[0];
    if (!pr.filter.dst_addr.empty())
        addr_frag += std::string(pr.filter.negate_dst_addr ? " !" : "") + " -d " + pr.filter.dst_addr[0];
    std::string pp = build_proto_port_fragment(pr.filter.proto,
                                               pr.filter.src_port,
                                               pr.filter.dst_port,
                                               pr.filter.negate_src_port,
                                               pr.filter.negate_dst_port);

    std::vector<std::string> lines;
    lines.reserve(iface_frags.size());
    for (const auto& iface_frag : iface_frags) {
        if (pr.direct) {
            if (pr.action == PendingRule::Mark) {
                lines.push_back(keen_pbr3::format(
                    "-A {}{}{}{} -j MARK --set-mark {:#x}\n",
                    CHAIN_NAME,
                    iface_frag,
                    addr_frag,
                    pp,
                    pr.fwmark));
            } else if (pr.action == PendingRule::Drop) {
                lines.push_back(keen_pbr3::format(
                    "-A {}{}{}{} -j DROP\n",
                    CHAIN_NAME,
                    iface_frag,
                    addr_frag,
                    pp));
            } else {
                lines.push_back(keen_pbr3::format(
                    "-A {}{}{}{} -j RETURN\n",
                    CHAIN_NAME,
                    iface_frag,
                    addr_frag,
                    pp));
            }
        } else {
            if (pr.action == PendingRule::Mark) {
                lines.push_back(keen_pbr3::format(
                    "-A {} -m set --match-set {} dst{}{}{} -j MARK --set-mark {:#x}\n",
                    CHAIN_NAME,
                    pr.set_name,
                    iface_frag,
                    addr_frag,
                    pp,
                    pr.fwmark));
            } else if (pr.action == PendingRule::Drop) {
                lines.push_back(keen_pbr3::format(
                    "-A {} -m set --match-set {} dst{}{}{} -j DROP\n",
                    CHAIN_NAME,
                    pr.set_name,
                    iface_frag,
                    addr_frag,
                    pp));
            } else {
                lines.push_back(keen_pbr3::format(
                    "-A {} -m set --match-set {} dst{}{}{} -j RETURN\n",
                    CHAIN_NAME,
                    pr.set_name,
                    iface_frag,
                    addr_frag,
                    pp));
            }
        }
    }

    return lines;
}

std::string IptablesFirewall::build_ipt_script(bool ipv6,
                                                const std::vector<PendingRule>& rules,
                                                const FirewallGlobalPrefilter& prefilter) {
    std::string s;
    s += keen_pbr3::format("*mangle\n:{} - [0:0]\n-A PREROUTING -j {}\n",
                           CHAIN_NAME, CHAIN_NAME);
    s += build_prefilter_lines(prefilter);
    for (const auto& pr : rules) {
        if (pr.ipv6 != ipv6) continue;
        for (const auto& line : build_rule_lines(pr, prefilter)) {
            s += line;
        }
    }
    s += "COMMIT\n";
    return s;
}

void IptablesFirewall::apply() {
    // Phase 1: ipsets via 'ipset restore -exist'
    {
        std::string ipset_script;
        for (const auto& ps : pending_sets_) {
            ipset_script += build_ipset_create_line(ps);
        }
        for (auto& [set_name, buf] : pending_elements_) {
            std::string elements = buf.str();
            if (!elements.empty()) {
                ipset_script += elements;
            }
        }
        if (!ipset_script.empty()) {
            pipe_to_cmd({"ipset", "restore", "-exist"}, ipset_script);
        }
    }

    // Phase 2: iptables rules via iptables-restore / ip6tables-restore.
    // Always materialize the KeenPbrTable scaffold for both protocols so
    // diagnostics can verify chain/jump presence even when no rules are needed.
    bool has_v4 = true;
    bool has_v6 = true;
    for (const auto& pr : pending_rules_) {
        if (pr.ipv6) has_v6 = true;
        else has_v4 = true;
    }

    if (has_v4) {
        pipe_to_cmd({"iptables-restore", "--noflush"},
                    build_ipt_script(false, pending_rules_, global_prefilter_));
        chain_v4_created_ = true;
    }
    if (has_v6) {
        pipe_to_cmd({"ip6tables-restore", "--noflush"},
                    build_ipt_script(true, pending_rules_, global_prefilter_));
        chain_v6_created_ = true;
    }

    // Clear pending buffers
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

void IptablesFirewall::cleanup() {
    auto& log = Logger::instance();

    // Remove jump rules, flush and delete custom chain for IPv4
    if (chain_v4_created_) {
        log.verbose("iptables cleanup: removing IPv4 chain {}", CHAIN_NAME);
        safe_exec({"iptables", "-t", "mangle", "-D", "PREROUTING", "-j", CHAIN_NAME}, /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-F", CHAIN_NAME}, /*suppress_output=*/true);
        safe_exec({"iptables", "-t", "mangle", "-X", CHAIN_NAME}, /*suppress_output=*/true);
        chain_v4_created_ = false;
    }

    // Same for IPv6
    if (chain_v6_created_) {
        log.verbose("iptables cleanup: removing IPv6 chain {}", CHAIN_NAME);
        safe_exec({"ip6tables", "-t", "mangle", "-D", "PREROUTING", "-j", CHAIN_NAME}, /*suppress_output=*/true);
        safe_exec({"ip6tables", "-t", "mangle", "-F", CHAIN_NAME}, /*suppress_output=*/true);
        safe_exec({"ip6tables", "-t", "mangle", "-X", CHAIN_NAME}, /*suppress_output=*/true);
        chain_v6_created_ = false;
    }

    // Destroy all created ipsets
    for (const auto& [name, _] : created_sets_) {
        log.verbose("iptables cleanup: destroying ipset {}", name);
        safe_exec({"ipset", "flush", name}, /*suppress_output=*/true);
        safe_exec({"ipset", "destroy", name}, /*suppress_output=*/true);
    }
    created_sets_.clear();

    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

FirewallBackend IptablesFirewall::backend() const {
    return FirewallBackend::iptables;
}

std::optional<bool> IptablesFirewall::test_ip_in_set(const std::string& set_name,
                                                       const std::string& ip) const {
    int exit_code = safe_exec({"ipset", "test", set_name, ip}, /*suppress_output=*/true);
    if (exit_code == 127) return std::nullopt; // ipset not installed
    return exit_code == 0;
}

std::unique_ptr<Firewall> create_iptables_firewall() {
    return std::make_unique<IptablesFirewall>();
}

} // namespace keen_pbr3
