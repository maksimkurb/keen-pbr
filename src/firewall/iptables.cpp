#include "iptables.hpp"
#include "ipset_restore_pipe.hpp"
#include "port_spec_util.hpp"
#include "../log/logger.hpp"
#include "../util/format_compat.hpp"
#include "../util/safe_exec.hpp"

#include <optional>
#include <string>
#include <sys/socket.h>

namespace keen_pbr3 {

namespace {

bool is_ipv6_addr(const std::string& addr) {
    return addr.find(':') != std::string::npos;
}

std::vector<std::string> filter_addrs_by_family(const std::vector<std::string>& addrs,
                                                bool ipv6) {
    std::vector<std::string> filtered;
    for (const auto& addr : addrs) {
        if (is_ipv6_addr(addr) == ipv6) {
            filtered.push_back(addr);
        }
    }
    return filtered;
}

std::vector<L4Proto> expand_l4_protos(L4Proto proto) {
    if (proto == L4Proto::TcpUdp) {
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    return {proto};
}

std::vector<L4Proto> expand_l4_protos_for_iptables(const FirewallRuleCriteria& criteria) {
    if (criteria.proto == L4Proto::Any
        && (!criteria.src_port.empty() || !criteria.dst_port.empty())) {
        // iptables requires an explicit L4 protocol whenever port matchers are used.
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    return expand_l4_protos(criteria.proto);
}

} // namespace

IptablesFirewall::IptablesFirewall() = default;

IptablesFirewall::~IptablesFirewall() {
    try {
        cleanup_impl();
    } catch (const std::exception& e) {
        Logger::instance().error("IptablesFirewall cleanup failed during destruction: {}",
                                 e.what());
    } catch (...) {
        Logger::instance().error(
            "IptablesFirewall cleanup failed during destruction: unknown error");
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

void IptablesFirewall::append_rules_for_family(bool ipv6,
                                               PendingRule::Action action,
                                               uint32_t fwmark,
                                               const FirewallRuleCriteria& criteria) {
    const std::vector<std::string> any_addr{""};
    const auto filtered_src_addrs = criteria.src_addr.empty()
        ? any_addr
        : filter_addrs_by_family(criteria.src_addr, ipv6);
    const auto filtered_dst_addrs = criteria.dst_addr.empty()
        ? any_addr
        : filter_addrs_by_family(criteria.dst_addr, ipv6);
    if ((!criteria.src_addr.empty() && filtered_src_addrs.empty())
        || (!criteria.dst_addr.empty() && filtered_dst_addrs.empty())) {
        return;
    }

    for (const auto proto : expand_l4_protos_for_iptables(criteria)) {
        const std::vector<std::string>& src_addrs = filtered_src_addrs;
        const std::vector<std::string>& dst_addrs = filtered_dst_addrs;
        for (const auto& src : src_addrs) {
            for (const auto& dst : dst_addrs) {
                PendingRule pr;
                pr.ipv6     = ipv6;
                pr.action   = action;
                pr.fwmark   = fwmark;
                pr.criteria = criteria;
                pr.criteria.proto = proto;
                pr.criteria.src_addr = src.empty()
                    ? std::vector<std::string>{}
                    : std::vector<std::string>{src};
                pr.criteria.dst_addr = dst.empty()
                    ? std::vector<std::string>{}
                    : std::vector<std::string>{dst};
                pending_rules_.push_back(std::move(pr));
            }
        }
    }
}

void IptablesFirewall::create_mark_rule(uint32_t fwmark,
                                        const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
        append_rules_for_family(ipv6, PendingRule::Mark, fwmark, criteria);
        return;
    }
    append_rules_for_family(false, PendingRule::Mark, fwmark, criteria);
    append_rules_for_family(true, PendingRule::Mark, fwmark, criteria);
}

void IptablesFirewall::create_drop_rule(const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
        append_rules_for_family(ipv6, PendingRule::Drop, 0, criteria);
        return;
    }
    append_rules_for_family(false, PendingRule::Drop, 0, criteria);
    append_rules_for_family(true, PendingRule::Drop, 0, criteria);
}

void IptablesFirewall::create_pass_rule(const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);
        append_rules_for_family(ipv6, PendingRule::Pass, 0, criteria);
        return;
    }
    append_rules_for_family(false, PendingRule::Pass, 0, criteria);
    append_rules_for_family(true, PendingRule::Pass, 0, criteria);
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

std::string IptablesFirewall::build_proto_port_fragment(L4Proto proto,
                                                         const PortSpec& src_port,
                                                         const PortSpec& dst_port,
                                                         bool negate_src_port,
                                                         bool negate_dst_port) {
    if (proto == L4Proto::Any && src_port.empty() && dst_port.empty()) {
        return "";
    }

    std::string frag;

    // -p flag (required if we have any port matching)
    if (proto != L4Proto::Any) {
        frag += " -p ";
        frag += l4_proto_name(proto);
    }

    bool has_src = !src_port.empty();
    bool has_dst = !dst_port.empty();
    bool src_list = has_src && classify_port_spec(src_port) == PortSpecKind::List;
    bool dst_list = has_dst && classify_port_spec(dst_port) == PortSpecKind::List;
    std::string normalized_src = has_src ? src_port.to_iptables_string() : "";
    std::string normalized_dst = has_dst ? dst_port.to_iptables_string() : "";

    // Use -m multiport only for comma-separated port lists. Single ports and
    // ranges are natively supported by the protocol matcher.
    if (has_src || has_dst) {
        if (src_list || dst_list) {
            if (src_list) {
                frag += " -m multiport" + std::string(negate_src_port ? " !" : "") + " --sports " + normalized_src;
            } else if (has_src) {
                frag += std::string(negate_src_port ? " !" : "") + " --sport " + normalized_src;
            }

            if (dst_list) {
                frag += " -m multiport" + std::string(negate_dst_port ? " !" : "") + " --dports " + normalized_dst;
            } else if (has_dst) {
                frag += std::string(negate_dst_port ? " !" : "") + " --dport " + normalized_dst;
            }
        } else {
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
            "-A {} -m conntrack --ctstate DNAT -j RETURN\n",
            CHAIN_NAME);
    }

    if (prefilter.has_inbound_interfaces()
        && prefilter.inbound_interfaces.has_value()
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
        && prefilter.inbound_interfaces.has_value()
        && prefilter.inbound_interfaces->size() > 1) {
        iface_frags.reserve(prefilter.inbound_interfaces->size());
        for (const auto& iface : *prefilter.inbound_interfaces) {
            iface_frags.push_back(" -i " + iface);
        }
    } else {
        iface_frags.push_back("");
    }

    std::string addr_frag;
    if (!pr.criteria.src_addr.empty())
        addr_frag += std::string(pr.criteria.negate_src_addr ? " !" : "") + " -s " + pr.criteria.src_addr[0];
    if (!pr.criteria.dst_addr.empty())
        addr_frag += std::string(pr.criteria.negate_dst_addr ? " !" : "") + " -d " + pr.criteria.dst_addr[0];
    std::vector<std::string> lines;
    lines.reserve(iface_frags.size() * 2);
    for (const auto proto : expand_l4_protos_for_iptables(pr.criteria)) {
        std::string pp = build_proto_port_fragment(proto,
                                                   pr.criteria.src_port,
                                                   pr.criteria.dst_port,
                                                   pr.criteria.negate_src_port,
                                                   pr.criteria.negate_dst_port);

        for (const auto& iface_frag : iface_frags) {
            if (!pr.criteria.dst_set_name.has_value()) {
                if (pr.action == PendingRule::Mark) {
                    lines.push_back(keen_pbr3::format(
                        "-A {}{}{}{} -j MARK --set-mark {:#x}\n",
                        CHAIN_NAME,
                        iface_frag,
                        addr_frag,
                        pp,
                        pr.fwmark));
                    lines.push_back(keen_pbr3::format(
                        "-A {}{}{}{} -j RETURN\n",
                        CHAIN_NAME,
                        iface_frag,
                        addr_frag,
                        pp));
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
                        *pr.criteria.dst_set_name,
                        iface_frag,
                        addr_frag,
                        pp,
                        pr.fwmark));
                    lines.push_back(keen_pbr3::format(
                        "-A {} -m set --match-set {} dst{}{}{} -j RETURN\n",
                        CHAIN_NAME,
                        *pr.criteria.dst_set_name,
                        iface_frag,
                        addr_frag,
                        pp));
                } else if (pr.action == PendingRule::Drop) {
                    lines.push_back(keen_pbr3::format(
                        "-A {} -m set --match-set {} dst{}{}{} -j DROP\n",
                        CHAIN_NAME,
                        *pr.criteria.dst_set_name,
                        iface_frag,
                        addr_frag,
                        pp));
                } else {
                    lines.push_back(keen_pbr3::format(
                        "-A {} -m set --match-set {} dst{}{}{} -j RETURN\n",
                        CHAIN_NAME,
                        *pr.criteria.dst_set_name,
                        iface_frag,
                        addr_frag,
                        pp));
                }
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

void IptablesFirewall::apply(FirewallApplyMode mode) {
    if (mode == FirewallApplyMode::Destructive) {
        cleanup_live_impl();
    } else {
        cleanup_rules_impl();
    }

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

void IptablesFirewall::cleanup_rules_impl() {
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
}

void IptablesFirewall::cleanup_live_impl() {
    auto& log = Logger::instance();

    cleanup_rules_impl();

    // Destroy all created ipsets
    for (const auto& [name, _] : created_sets_) {
        log.verbose("iptables cleanup: destroying ipset {}", name);
        safe_exec({"ipset", "flush", name}, /*suppress_output=*/true);
        safe_exec({"ipset", "destroy", name}, /*suppress_output=*/true);
    }
}

void IptablesFirewall::cleanup_impl() {
    cleanup_live_impl();

    created_sets_.clear();

    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

void IptablesFirewall::cleanup() {
    cleanup_impl();
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
