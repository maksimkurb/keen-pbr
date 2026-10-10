#pragma once

#include "dnsmasq_types.hpp"
#include "../util/safe_exec.hpp"

#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct DnsmasqBinaryIdentity {
    std::string path;
    dev_t dev{0};
    ino_t ino{0};
    off_t size{0};
    std::int64_t mtime_ns{0};
    bool operator==(const DnsmasqBinaryIdentity& o) const {
        return path == o.path && dev == o.dev && ino == o.ino && size == o.size &&
               mtime_ns == o.mtime_ns;
    }
    bool operator!=(const DnsmasqBinaryIdentity& o) const { return !(*this == o); }
};

// stat(path); std::nullopt when stat fails.
std::optional<DnsmasqBinaryIdentity> dnsmasq_binary_identity(const std::string& path);

enum class DnsmasqCapabilityStatus { NotChecked, Ok, Unsupported, Failed };
const char* capability_status_name(DnsmasqCapabilityStatus s);  // "not_checked","ok","unsupported","failed"

struct DnsmasqCapabilityResult {
    DnsmasqCapabilityStatus status{DnsmasqCapabilityStatus::NotChecked};
    DnsmasqErrorCode code{DnsmasqErrorCode::None};
    std::string detail;   // bounded dnsmasq output (<= 512 bytes)
};

using DnsmasqTimedExecFn =
    std::function<ExecCaptureResult(const std::vector<std::string>& args, std::chrono::milliseconds timeout)>;

// Default exec: safe_exec_capture_with_timeouts(args, {timeout, 2 s}, true, 4096, true).
ExecCaptureResult dnsmasq_default_timed_exec(const std::vector<std::string>& args,
                                             std::chrono::milliseconds timeout);

// Last `max_bytes` bytes of `text`.
std::string dnsmasq_output_tail(const std::string& text, std::size_t max_bytes = 512);

struct DnsmasqCapabilityDeps {
    DnsmasqTimedExecFn exec;  // empty -> dnsmasq_default_timed_exec
    std::string tmp_root{"/tmp"};
};

constexpr std::chrono::seconds kCapabilityTimeout{10};

// Runs only keen-pbr's own controlled files. Never throws.
DnsmasqCapabilityResult probe_dnsmasq_capabilities(const std::string& dnsmasq_path,
                                                   const DnsmasqCapabilityDeps& deps = {});

} // namespace keen_pbr3
