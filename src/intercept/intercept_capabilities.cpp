#include "intercept_capabilities.hpp"

#include "../log/logger.hpp"
#include "../util/safe_exec.hpp"

#include <sys/utsname.h>

#include <exception>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace keen_pbr3 {

namespace {

// Names listed in a /proc/net/ip*_tables_{targets,matches} file (one per line).
std::set<std::string> read_names(const std::string& path) {
    std::set<std::string> names;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string name;
        if (fields >> name) {
            names.insert(std::move(name));
        }
    }
    return names;
}

struct Requirement {
    const char* what;     // item for the reason string
    const char* module;   // kernel module that provides it
    bool target;          // true: ip*_tables_targets, false: ip*_tables_matches
    const char* name;     // entry name in the proc file
};

constexpr Requirement kNfqueue{"NFQUEUE target", "xt_NFQUEUE", true, "NFQUEUE"};
constexpr Requirement kConntrack{"conntrack match", "xt_conntrack", false, "conntrack"};
constexpr Requirement kNflog{"NFLOG target", "xt_NFLOG", true, "NFLOG"};
constexpr Requirement kConnbytes{"connbytes match", "xt_connbytes", false, "connbytes"};

class Prober {
public:
    Prober(bool ipv6_enabled, const InterceptProbeEnv& env)
        : ipv6_enabled_(ipv6_enabled), env_(env) {}

    bool available(const Requirement& req) {
        if (present(req)) {
            return true;
        }
        if (tried_modules_.insert(req.module).second) {
            load_module(req.module);
        }
        return present(req);
    }

private:
    bool present(const Requirement& req) const {
        const auto& v4 = read_names(req.target ? env_.ip_targets : env_.ip_matches);
        if (v4.count(req.name) == 0) return false;
        if (!ipv6_enabled_) return true;
        const auto& v6 = read_names(req.target ? env_.ip6_targets : env_.ip6_matches);
        return v6.count(req.name) > 0;
    }

    void load_module(const char* module) const {
        if (env_.modprobe) {
            env_.modprobe(module);
            return;
        }
        // Loadable but not yet loaded modules do not show up in /proc.
        (void)safe_exec({"modprobe", module}, /*suppress_output=*/true);
    }

    bool ipv6_enabled_;
    const InterceptProbeEnv& env_;
    std::set<std::string> tried_modules_;
};

std::unique_ptr<nfnl::SetWriterTransport> open_transport(const InterceptProbeEnv& env) {
    return env.make_transport ? env.make_transport() : nfnl::make_probe_transport();
}

std::string kernel_release_of(const InterceptProbeEnv& env) {
    if (env.kernel_release) return env.kernel_release();
    utsname info{};
    if (::uname(&info) != 0) return {};
    return info.release;
}

// Runs `fn` with a fresh transport; socket failures become probe errors.
template <typename Fn>
nfnl::ProbeResult with_transport(const InterceptProbeEnv& env, const char* what, Fn&& fn) {
    try {
        auto transport = open_transport(env);
        if (!transport) {
            return nfnl::make_probe_result(nfnl::ProbeStatus::error,
                                           std::string(what) + ": no netlink transport");
        }
        return fn(*transport);
    } catch (const std::exception& error) {
        return nfnl::make_probe_result(nfnl::ProbeStatus::error,
                                       std::string(what) + ": " + error.what());
    }
}

void run_runtime_probes(InterceptRuntimeProbe& probe, FirewallBackend backend,
                        const InterceptProbeEnv& env) {
    probe.kernel_release = kernel_release_of(env);
    if (!env.runtime_probes) return;

    if (backend == FirewallBackend::nftables) {
        probe.set_backend = with_transport(env, "nf_tables", [&](nfnl::SetWriterTransport& t) {
            return nfnl::probe_nft_tables(t, env.nft_table, env.probe_timeout_ms);
        });
    } else {
        probe.set_backend = with_transport(env, "ipset", [&](nfnl::SetWriterTransport& t) {
            const auto result = nfnl::probe_ipset_protocol(t, env.probe_timeout_ms);
            probe.ipset_protocol = result.protocol;
            return result.result;
        });
    }
    probe.conntrack = with_transport(env, "ctnetlink", [&](nfnl::SetWriterTransport& t) {
        return nfnl::probe_ctnetlink(t, env.probe_timeout_ms);
    });
}

const char* replacement_status_reason(nfnl::ReplacementCapability capability) {
    switch (capability) {
    case nfnl::ReplacementCapability::supported:
        return "network namespace is owned by the initial user namespace";
    case nfnl::ReplacementCapability::unsupported:
        return "network namespace is owned by a non-initial user namespace; "
               "the kernel would turn NFQA_PAYLOAD verdicts into DROP";
    case nfnl::ReplacementCapability::unknown:
        break;
    }
    return "cannot determine the namespace owner (no NS_GET_USERNS, not the initial namespace)";
}

} // namespace

std::vector<InterceptRuntimeProbe::Item> InterceptRuntimeProbe::items() const {
    using nfnl::ProbeStatus;
    nfnl::ProbeResult replacement_result;
    switch (replacement) {
    case nfnl::ReplacementCapability::supported:
        replacement_result = nfnl::make_probe_result(ProbeStatus::ok,
                                                     replacement_status_reason(replacement));
        break;
    case nfnl::ReplacementCapability::unsupported:
        replacement_result = nfnl::make_probe_result(ProbeStatus::unsupported,
                                                     replacement_status_reason(replacement));
        break;
    case nfnl::ReplacementCapability::unknown:
        // Only meaningful once a queue was bound.
        if (nfqueue.status != ProbeStatus::not_run) {
            replacement_result = nfnl::make_probe_result(ProbeStatus::skipped,
                                                         replacement_status_reason(replacement));
        }
        break;
    }
    return {
        {"set_backend", set_backend},
        {"nfqueue", nfqueue},
        {"fail_open", fail_open},
        {"payload_replacement", replacement_result},
        {"nflog", nflog},
        {"set_write", set_write},
        {"conntrack", conntrack},
        {"nft_timeout_update", timeout_update},
    };
}

namespace {
// Prefers an IPv4 dynamic set; the element is a reserved documentation address.
const FirewallSetDeclaration* choose_probe_set(const std::vector<FirewallSetDeclaration>& sets) {
    const FirewallSetDeclaration* chosen = nullptr;
    for (const auto& set : sets) {
        const bool v4 = set.name.rfind("kpbr4d_", 0) == 0;
        const bool v6 = set.name.rfind("kpbr6d_", 0) == 0;
        if (!v4 && !v6) continue;
        if (chosen == nullptr || (v4 && chosen->name.rfind("kpbr4d_", 0) != 0)) chosen = &set;
    }
    return chosen;
}

nfnl::SetAdd probe_element(const FirewallSetDeclaration& set, uint8_t last_octet) {
    nfnl::SetAdd element;
    element.set_name = set.name;
    element.timeout_s = 1;
    if (set.name.rfind("kpbr6d_", 0) == 0) {
        element.family = 6;
        element.addr = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, last_octet};
    } else {
        element.family = 4;
        element.addr = {192, 0, 2, last_octet};
    }
    return element;
}
} // namespace

nfnl::ProbeResult probe_intercept_nft_timeout_update(
    FirewallBackend backend, const std::vector<FirewallSetDeclaration>& sets,
    const InterceptProbeEnv& env) {
    if (!env.runtime_probes || backend != FirewallBackend::nftables) return {};
    const FirewallSetDeclaration* chosen = choose_probe_set(sets);
    if (chosen == nullptr) {
        return nfnl::make_probe_result(nfnl::ProbeStatus::skipped,
                                       "no dynamic set exists to test against");
    }
    const nfnl::SetAdd element = probe_element(*chosen, 0xfe);
    return with_transport(env, "nft timeout update", [&](nfnl::SetWriterTransport& t) {
        return nfnl::probe_nft_timeout_update(t, env.nft_table, element, env.probe_timeout_ms);
    });
}

nfnl::ProbeResult probe_intercept_set_write(FirewallBackend backend,
                                            const std::vector<FirewallSetDeclaration>& sets,
                                            const InterceptProbeEnv& env) {
    if (!env.runtime_probes) return {};
    const FirewallSetDeclaration* chosen = choose_probe_set(sets);
    if (chosen == nullptr) {
        return nfnl::make_probe_result(nfnl::ProbeStatus::skipped,
                                       "no dynamic set exists to test against");
    }
    const nfnl::SetAdd element = probe_element(*chosen, 0xff);
    const bool nft = backend == FirewallBackend::nftables;
    auto result = with_transport(env, "set write", [&](nfnl::SetWriterTransport& t) {
        return nfnl::probe_set_write(t, nft, env.nft_table, element, env.probe_timeout_ms);
    });
    return result;
}

void load_intercept_modules(FirewallBackend backend, const InterceptProbeEnv& env) {
    // Best effort, failures ignored: the netlink subsystems are not always
    // autoloaded (OpenWrt) and a missing module is reported at bind time.
    std::vector<const char*> modules{"nfnetlink_queue", "nfnetlink_log"};
    if (backend == FirewallBackend::nftables) {
        modules.insert(modules.end(), {"nft_queue", "nft_log", "nft_ct"});
    }
    for (const char* module : modules) {
        if (env.modprobe) {
            env.modprobe(module);
        } else {
            (void)safe_exec({"modprobe", module}, /*suppress_output=*/true);
        }
    }
}

InterceptCapabilities probe_intercept_capabilities(FirewallBackend backend,
                                                   bool ipv6_enabled,
                                                   const InterceptProbeEnv& env) {
    InterceptCapabilities caps;
    load_intercept_modules(backend, env);
    run_runtime_probes(caps.probe, backend, env);
    if (backend == FirewallBackend::nftables) {
        caps.nfqueue = true;
        caps.nflog = true;
        caps.connbytes = true;
        return caps;
    }

    Prober prober(ipv6_enabled, env);
    std::vector<std::string> missing;
    const auto need = [&](const Requirement& req) {
        const bool ok = prober.available(req);
        if (!ok) missing.emplace_back(req.what);
        return ok;
    };
    const bool queue_target = need(kNfqueue);
    const bool conntrack = need(kConntrack);
    caps.nfqueue = queue_target && conntrack;
    caps.nflog = need(kNflog);
    caps.connbytes = need(kConnbytes);

    for (const auto& item : missing) {
        if (!caps.reason.empty()) caps.reason += ", ";
        caps.reason += item;
    }
    if (!caps.reason.empty()) {
        caps.reason = "kernel lacks: " + caps.reason;
        Logger::instance().warn("Interception capability probe: {}", caps.reason);
    }
    return caps;
}

bool enable_conntrack_accounting(const std::string& path) {
    std::ofstream out(path);
    if (out) {
        out << "1\n";
        out.flush();
    }
    if (!out) {
        Logger::instance().warn(
            "Cannot enable conntrack accounting via {}; nftables L7 sniffing "
            "(ct original packets) may not match",
            path);
        return false;
    }
    return true;
}

} // namespace keen_pbr3
