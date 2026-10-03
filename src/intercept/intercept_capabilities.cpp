#include "intercept_capabilities.hpp"

#include "../log/logger.hpp"
#include "../util/safe_exec.hpp"

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

} // namespace

InterceptCapabilities probe_intercept_capabilities(FirewallBackend backend,
                                                   bool ipv6_enabled,
                                                   const InterceptProbeEnv& env) {
    InterceptCapabilities caps;
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
