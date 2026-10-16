#pragma once
#include "dnsmasq_types.hpp"
#include "../dns/dns_txt_probe.hpp"
#include "../util/safe_exec.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct InterfaceAddress { std::string ifname; int family{4}; std::string address; };
std::vector<InterfaceAddress> system_interface_addresses();

struct DnsmasqDetectorDeps {
    // Runs the hook; args[0] is the hook path. Default: safe_exec_capture(args,
    // true, 16384, true) — inspect output can be longer than 2 KiB.
    std::function<ExecCaptureResult(const std::vector<std::string>&)> exec;
    std::string proc_root{"/proc"};
    // config-hash.keen.pbr IN TXT. Default: probe_dns_txt(ep.address, ep.port, kDnsmasqStampDomain, t).
    std::function<DnsTxtProbeResult(const DnsmasqEndpoint&, std::chrono::milliseconds)> probe_txt;
    // version.bind CH TXT answered at all. Default: dns_probe_responding(ep, t).
    std::function<bool(const DnsmasqEndpoint&, std::chrono::milliseconds)> probe_responding;
    std::function<std::vector<InterfaceAddress>()> interface_addresses;
};

struct DnsmasqDetectOptions {
    std::optional<std::string> configured_selector;   // dns.dnsmasq_instance
    std::string expected_hash;                        // empty: accept any stamp
    std::optional<DnsmasqEndpoint> previous_endpoint;
    bool probe{true};                                 // false: skip §8.6 probing
    bool probe_all{false};                            // CLI detect: evaluate every instance
};

class DnsmasqDetector {
public:
    explicit DnsmasqDetector(std::string hook_path, DnsmasqDetectorDeps deps = {});
    // Never throws. Read-only.
    DnsmasqDetection detect(const DnsmasqDetectOptions& options) const;

private:
    std::string hook_path_;
    DnsmasqDetectorDeps deps_;
};

} // namespace keen_pbr3
