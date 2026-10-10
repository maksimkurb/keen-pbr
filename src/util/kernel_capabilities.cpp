#include "kernel_capabilities.hpp"

#include "../firewall/iptables.hpp"
#include "../log/logger.hpp"
#include "ipv6_support.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

bool on_path(const std::string& name) {
    const char* env = std::getenv("PATH");
    // Same fallback execvp() uses when PATH is unset.
    const std::string path = env != nullptr ? env : "/usr/local/bin:/usr/bin:/bin";
    std::istringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        const std::string candidate = (dir.empty() ? "." : dir) + "/" + name;
        struct stat st {};
        if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            ::access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

HostTools find_host_tools() {
    HostTools tools;
#ifndef KEEN_PBR_PLATFORM_KEENETIC
    tools.nft = on_path("nft");
#endif
    tools.iptables = on_path("iptables");
    tools.iptables_restore = on_path("iptables-restore");
    tools.ip6tables = on_path("ip6tables");
    tools.ip6tables_restore = on_path("ip6tables-restore");
    tools.ipset = on_path("ipset");
    return tools;
}

std::mutex g_mutex;
std::shared_ptr<const HostTools> g_tools;
std::shared_ptr<const KernelCapabilities> g_snapshot;
std::atomic<std::uint64_t> g_probe_runs{0};

} // namespace

std::optional<FirewallBackend> HostTools::detected_backend() const {
#ifndef KEEN_PBR_PLATFORM_KEENETIC
    if (nft) return FirewallBackend::nftables;
#endif
    if (iptables) return FirewallBackend::iptables;
    return std::nullopt;
}

std::string HostTools::missing_required(FirewallBackend backend) const {
    std::string missing;
    const auto need = [&missing](bool present, const char* name) {
        if (present) return;
        if (!missing.empty()) missing += ", ";
        missing += name;
    };
    switch (backend) {
    case FirewallBackend::nftables:
        need(nft, "nft");
        break;
    case FirewallBackend::iptables:
        need(iptables, "iptables");
        need(iptables_restore, "iptables-restore");
        need(ipset, "ipset");
        break;
    }
    return missing;
}

const HostTools& host_tools() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_tools) g_tools = std::make_shared<const HostTools>(find_host_tools());
    // Replaced only by tests, which never race readers.
    return *g_tools;
}

KernelCapabilities probe_kernel_capabilities(const HostTools& tools) {
    g_probe_runs.fetch_add(1, std::memory_order_relaxed);
    KernelCapabilities caps;
    caps.tools = tools;
    caps.system_ipv6 = system_ipv6_supported();

    if (tools.iptables_restore) {
#ifndef KEEN_PBR_PLATFORM_KEENETIC
        caps.xt_statistic = probe_iptables_statistic();
#endif
        caps.xt_comment_v4 = IptablesFirewall::probe_xt_comment_support(false);
        caps.nflog_size_v4 = IptablesFirewall::probe_nflog_size_support(false);
    }
    if (tools.ip6tables && tools.ip6tables_restore && caps.system_ipv6) {
        caps.iptables_ipv6 = iptables_ipv6_supported();
        if (caps.iptables_ipv6) {
            caps.xt_comment_v6 = IptablesFirewall::probe_xt_comment_support(true);
            caps.nflog_size_v6 = IptablesFirewall::probe_nflog_size_support(true);
        }
    }
#ifndef KEEN_PBR_PLATFORM_KEENETIC
    if (tools.nft && caps.system_ipv6) {
        caps.nftables_ipv6 = nft_ipv6_supported();
    }
#endif

    Logger::instance().info(
        "Kernel capabilities (probed once): ipv6 system={} iptables={} nftables={}; "
        "xt_comment v4={} v6={}; nflog_size v4={} v6={}; xt_statistic={}",
        caps.system_ipv6, caps.iptables_ipv6, caps.nftables_ipv6, caps.xt_comment_v4,
        caps.xt_comment_v6, caps.nflog_size_v4, caps.nflog_size_v6, caps.xt_statistic);
    return caps;
}

std::shared_ptr<const KernelCapabilities> kernel_capabilities() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_snapshot) return g_snapshot;
    }
    // Probe outside the lock: host_tools() takes it, and the probes spawn
    // processes.  A concurrent first use probes twice at worst; the first one
    // to finish wins and the other answer is dropped.
    auto fresh = std::make_shared<const KernelCapabilities>(
        probe_kernel_capabilities(host_tools()));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_snapshot) g_snapshot = std::move(fresh);
    return g_snapshot;
}

std::uint64_t kernel_capability_probe_runs() {
    return g_probe_runs.load(std::memory_order_relaxed);
}

#ifdef KEEN_PBR3_TESTING
void set_kernel_capabilities_for_tests(std::optional<KernelCapabilities> snapshot) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = snapshot ? std::make_shared<const KernelCapabilities>(*snapshot) : nullptr;
}

void set_host_tools_for_tests(std::optional<HostTools> tools) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tools = tools ? std::make_shared<const HostTools>(*tools) : nullptr;
}

void reset_kernel_capabilities_for_tests() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tools = nullptr;
    g_snapshot = nullptr;
}
#endif

} // namespace keen_pbr3
