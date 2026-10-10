#pragma once

#include "dnsmasq_capability.hpp"   // DnsmasqTimedExecFn
#include "dnsmasq_types.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace keen_pbr3 {

// M0 experiment D sets the final value: max(60 s, 3 x measured), rounded up to 10 s.
constexpr std::chrono::seconds kPreflightTimeout{120};

struct DnsmasqPreflightResult {
    bool ok{false};
    bool skipped{false};
    std::string detail;                    // <= 512 bytes of dnsmasq output on failure
    std::chrono::milliseconds duration{0};
    std::vector<std::string> argv;         // what was run (for logs)
};

struct DnsmasqPreflightDeps {
    DnsmasqTimedExecFn exec;               // empty -> dnsmasq_default_timed_exec
    std::string tmp_root{"/tmp"};
    std::chrono::milliseconds timeout{kPreflightTimeout};
};

// argv for `dnsmasq --test` that loads exactly what the running process loads,
// plus `candidate_file` (may be empty). Exposed for tests.
std::vector<std::string> build_preflight_argv(const ProcessInfo& process,
                                              const HookInstanceRecord& hook,
                                              const std::string& candidate_file);

// Requires inst.process. Never throws.
DnsmasqPreflightResult run_dnsmasq_preflight(const DnsmasqInstance& inst,
                                             const DnsmasqPreflightDeps& deps = {});

} // namespace keen_pbr3
