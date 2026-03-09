#include "iptables.hpp"
#include "ipset_restore_pipe.hpp"
#include "../log/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <format>
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

int IptablesFirewall::exec_cmd(const std::string& cmd) {
    return std::system(cmd.c_str());
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

std::unique_ptr<ListEntryVisitor> IptablesFirewall::create_batch_loader(
    const std::string& set_name, int32_t entry_timeout) {
    auto& buf = pending_elements_[set_name];
    return std::make_unique<IpsetRestoreVisitor>(buf, set_name, entry_timeout);
}

static void pipe_to_cmd(const std::string& cmd, const std::string& input) {
    Logger::instance().verbose("{} script:\n{}", cmd, input);
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        throw FirewallError(std::format("Failed to open pipe to '{}'", cmd));
    }
    if (std::fwrite(input.data(), 1, input.size(), pipe) != input.size()) {
        pclose(pipe);
        throw FirewallError(std::format("Failed to write to pipe for '{}'", cmd));
    }
    int status = pclose(pipe);
    if (status != 0) {
        throw FirewallError(std::format("{} exited with status {}", cmd, status));
    }
}

std::string IptablesFirewall::build_ipset_create_line(const PendingSet& ps) {
    if (ps.timeout > 0) {
        return std::format("create {} hash:net family {} timeout {} -exist\n",
                           ps.name, ps.family_str, ps.timeout);
    } else {
        return std::format("create {} hash:net family {} -exist\n",
                           ps.name, ps.family_str);
    }
}

// Convert a port spec token to iptables format.
// "443" → "443", "8000-9000" → "8000:9000" (iptables uses colon for range)
static std::string port_token_to_ipt(const std::string& token) {
    auto dash = token.find('-');
    if (dash != std::string::npos) {
        return token.substr(0, dash) + ":" + token.substr(dash + 1);
    }
    return token;
}

// Returns true if port_spec is a comma-separated list (contains ',').
static bool is_port_list(const std::string& port_spec) {
    return port_spec.find(',') != std::string::npos;
}

std::string IptablesFirewall::build_proto_port_fragment(const std::string& proto,
                                                         const std::string& src_port,
                                                         const std::string& dst_port) {
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
    bool src_list = has_src && is_port_list(src_port);
    bool dst_list = has_dst && is_port_list(dst_port);

    // Use -m multiport when: both ports present, or either is a comma list
    if (has_src || has_dst) {
        if (src_list || dst_list || (has_src && has_dst)) {
            frag += " -m multiport";
            if (has_src) frag += " --sports " + src_port;
            if (has_dst) frag += " --dports " + dst_port;
        } else {
            // Single port or range, single direction — use --sport/--dport
            if (has_src) frag += " --sport " + port_token_to_ipt(src_port);
            if (has_dst) frag += " --dport " + port_token_to_ipt(dst_port);
        }
    }

    return frag;
}

std::string IptablesFirewall::build_ipt_script(bool ipv6,
                                                const std::vector<PendingRule>& rules) {
    std::string s;
    s += std::format("*mangle\n:{} - [0:0]\n-A PREROUTING -j {}\n",
                     CHAIN_NAME, CHAIN_NAME);
    for (const auto& pr : rules) {
        if (pr.ipv6 != ipv6) continue;
        // Optional src/dst address constraints (-s/-d flags).
        // After expand_and_push each filter has at most one entry per list.
        std::string addr_frag;
        if (!pr.filter.src_addr.empty()) addr_frag += " -s " + pr.filter.src_addr[0];
        if (!pr.filter.dst_addr.empty()) addr_frag += " -d " + pr.filter.dst_addr[0];
        std::string pp = build_proto_port_fragment(pr.filter.proto,
                                                   pr.filter.src_port,
                                                   pr.filter.dst_port);
        if (pr.action == PendingRule::Mark) {
            s += std::format("-A {} -m set --match-set {} dst{}{} -j MARK --set-mark {:#x}\n",
                             CHAIN_NAME, pr.set_name, addr_frag, pp, pr.fwmark);
        } else {
            s += std::format("-A {} -m set --match-set {} dst{}{} -j DROP\n",
                             CHAIN_NAME, pr.set_name, addr_frag, pp);
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
            pipe_to_cmd("ipset restore -exist", ipset_script);
        }
    }

    // Phase 2: iptables rules via iptables-restore / ip6tables-restore
    // Collect rules by protocol
    bool has_v4 = false;
    bool has_v6 = false;
    for (const auto& pr : pending_rules_) {
        if (pr.ipv6) has_v6 = true;
        else has_v4 = true;
    }

    if (has_v4) {
        pipe_to_cmd("iptables-restore --noflush", build_ipt_script(false, pending_rules_));
        chain_v4_created_ = true;
    }
    if (has_v6) {
        pipe_to_cmd("ip6tables-restore --noflush", build_ipt_script(true, pending_rules_));
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
        exec_cmd(std::format("iptables -t mangle -D PREROUTING -j {} 2>/dev/null", CHAIN_NAME));
        exec_cmd(std::format("iptables -t mangle -F {} 2>/dev/null", CHAIN_NAME));
        exec_cmd(std::format("iptables -t mangle -X {} 2>/dev/null", CHAIN_NAME));
        chain_v4_created_ = false;
    }

    // Same for IPv6
    if (chain_v6_created_) {
        log.verbose("iptables cleanup: removing IPv6 chain {}", CHAIN_NAME);
        exec_cmd(std::format("ip6tables -t mangle -D PREROUTING -j {} 2>/dev/null", CHAIN_NAME));
        exec_cmd(std::format("ip6tables -t mangle -F {} 2>/dev/null", CHAIN_NAME));
        exec_cmd(std::format("ip6tables -t mangle -X {} 2>/dev/null", CHAIN_NAME));
        chain_v6_created_ = false;
    }

    // Destroy all created ipsets
    for (const auto& [name, _] : created_sets_) {
        log.verbose("iptables cleanup: destroying ipset {}", name);
        exec_cmd(std::format("ipset flush {} 2>/dev/null", name));
        exec_cmd(std::format("ipset destroy {} 2>/dev/null", name));
    }
    created_sets_.clear();

    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

FirewallBackend IptablesFirewall::backend() const {
    return FirewallBackend::iptables;
}

std::unique_ptr<Firewall> create_iptables_firewall() {
    return std::make_unique<IptablesFirewall>();
}

} // namespace keen_pbr3
