#pragma once

#include "../config/dnsmasq_selector.hpp"
#include "../dns/dnsmasq_gen.hpp"   // DnsmasqConfigStamp

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// ---------------------------------------------------------------- error codes
// Stable machine-readable codes. The string form (dnsmasq_error_code_name) is
// part of the API and of the i18n keys; never rename an existing value.
enum class DnsmasqErrorCode {
    None,
    NoHook,                 // DNSMASQ_NO_HOOK
    HookFailed,             // DNSMASQ_HOOK_FAILED
    NotInstalled,           // DNSMASQ_NOT_INSTALLED
    NoDnsInstance,          // DNSMASQ_NO_DNS_INSTANCE
    InstanceNotFound,       // DNSMASQ_INSTANCE_NOT_FOUND
    InstanceAmbiguous,      // DNSMASQ_INSTANCE_AMBIGUOUS
    NotRunning,             // DNSMASQ_NOT_RUNNING
    DnsDisabled,            // DNSMASQ_DNS_DISABLED
    OtherNetns,             // DNSMASQ_OTHER_NETNS
    NoDnsListener,          // DNSMASQ_NO_DNS_LISTENER
    EndpointUnresponsive,   // DNSMASQ_ENDPOINT_UNRESPONSIVE
    DropinNotLoaded,        // DNSMASQ_DROPIN_NOT_LOADED
    ForeignDropin,          // DNSMASQ_FOREIGN_DROPIN
    ConfScriptUnsupported,  // DNSMASQ_CONF_SCRIPT_UNSUPPORTED
    DnsmasqTestFailed,      // DNSMASQ_TEST_FAILED
    PreflightFailed,        // DNSMASQ_PREFLIGHT_FAILED
    ApplyFailed,            // DNSMASQ_APPLY_FAILED
    ConfigNotLoaded,        // DNSMASQ_CONFIG_NOT_LOADED
    StampMismatch,          // DNSMASQ_STAMP_MISMATCH
    RepairPaused,           // DNSMASQ_REPAIR_PAUSED
    ProbeFailed,            // DNSMASQ_PROBE_FAILED
};

// "DNSMASQ_NO_HOOK", ...; DnsmasqErrorCode::None -> "".
const char* dnsmasq_error_code_name(DnsmasqErrorCode code);

// ---------------------------------------------------------------- endpoint
struct DnsmasqEndpoint {
    std::string address;      // numeric IPv4 or IPv6, no brackets, no scope id
    std::uint16_t port{53};

    // "192.168.1.1:53" or "[::1]:53"
    std::string str() const;
    bool operator==(const DnsmasqEndpoint& o) const {
        return address == o.address && port == o.port;
    }
    bool operator!=(const DnsmasqEndpoint& o) const { return !(*this == o); }
};

// Parses "ip:port", "[ipv6]:port", or bare "ip" (port 53). Validates the IP
// with inet_pton. std::nullopt on any error.
std::optional<DnsmasqEndpoint> parse_dnsmasq_endpoint(std::string_view text);

// How strongly we know that `endpoint` belongs to the selected instance.
enum class EndpointEvidence {
    None,           // no candidate at all
    SocketOwned,    // a socket of the selected PID is bound there, nothing answered
    Responding,     // something answered DNS there (the PID owns the socket)
    StampVerified,  // it answered with a keen-pbr stamp whose hash == expected
};
const char* endpoint_evidence_name(EndpointEvidence e);  // "none", "socket_owned", "responding", "stamp_verified"

// ---------------------------------------------------------------- hook data
enum class DropinState { Absent, Current, Foreign };
const char* dropin_state_name(DropinState s);          // "absent", "current", "foreign"
std::optional<DropinState> parse_dropin_state(std::string_view s);

enum class DropinInclude {
    Static,   // dnsmasq already includes dropin_dir; must be checked before apply
    Managed,  // the hook's apply adds the include itself (Keenetic managed block)
};

// One `[instance]` record of `dnsmasq-hook.sh inspect` (§7.2).
struct HookInstanceRecord {
    std::string selector;                    // raw text; validated by the detector
    bool enabled{true};
    std::optional<pid_t> main_pid;           // 0 in the output -> std::nullopt
    std::string match_conf_file;             // match process by effective -C file
    std::string exe_required;                // if set, /proc/<pid>/exe must equal it
    std::string default_conf_file;           // dnsmasq's compiled default when no -C
    DropinInclude dropin_include{DropinInclude::Static};
    std::string dropin_dir;
    std::string dropin_file;
    std::string dropin_content;              // exact single line the hook installs
    DropinState dropin_state{DropinState::Absent};
    std::optional<DnsmasqEndpoint> static_endpoint;
    bool preflight_skip{false};              // only the netns harness sets this
    std::vector<std::string> owned_mounts;   // OpenWrt: keen_pbr_addnmount
    std::vector<std::string> missing_mounts; // OpenWrt: required but absent
};

struct HookInspectResult {
    int hook_version{0};
    std::string platform;    // "openwrt" | "keenetic" | "debian" | "test"
    std::string binary;      // dnsmasq executable path, empty if not installed
    std::vector<HookInstanceRecord> instances;
};

// ---------------------------------------------------------------- /proc data
struct ProcessInfo {
    pid_t pid{0};
    pid_t ppid{0};
    std::string exe;                 // readlink(/proc/<pid>/exe), " (deleted)" suffix stripped
    std::vector<std::string> argv;   // /proc/<pid>/cmdline split on '\0'
    std::string netns;               // readlink(/proc/<pid>/ns/net), e.g. "net:[4026531840]"
};

struct SocketListener {
    DnsmasqEndpoint local;   // address is "0.0.0.0" or "::" for wildcard sockets
    int family{4};           // 4 or 6
    bool udp{false};
    bool tcp{false};
    bool wildcard{false};
};

struct ConfDirSpec {
    std::string dir;                            // no trailing '/'
    std::vector<std::string> include_suffixes;  // from "*.conf" -> ".conf"
    std::vector<std::string> exclude_suffixes;  // from ".bak"   -> ".bak"
};

struct DnsmasqEffectiveConfig {
    std::vector<std::string> conf_files_read;   // in read order
    std::optional<std::uint16_t> port;          // last port= wins
    std::vector<std::string> listen_addresses;
    std::vector<std::string> interfaces;
    std::vector<std::string> except_interfaces;
    bool bind_interfaces{false};
    bool bind_dynamic{false};
    std::vector<ConfDirSpec> conf_dirs;
    std::vector<std::string> servers_files;     // recorded for M2, not used in M1
    std::vector<std::string> warnings;          // unreadable files, depth limit, ...
};

// ---------------------------------------------------------------- results
struct DnsmasqInstance {
    HookInstanceRecord hook;
    DnsmasqSelector selector;
    std::optional<ProcessInfo> process;         // unset: not running
    DnsmasqEffectiveConfig config;
    bool dropin_dir_included{false};            // conf_dirs include dropin_dir and accept its file name
    std::vector<SocketListener> listeners;
    std::vector<DnsmasqEndpoint> candidates;
    std::optional<DnsmasqEndpoint> endpoint;
    EndpointEvidence evidence{EndpointEvidence::None};
    std::optional<DnsmasqConfigStamp> stamp;    // stamp seen at `endpoint`, if any
    bool probed{false};                         // candidates were probed
    DnsmasqErrorCode problem{DnsmasqErrorCode::None};
    std::string problem_detail;
    std::vector<std::string> warnings;
};

enum class DnsmasqSelectionKind { None, Explicit, Adopted, Automatic };
const char* selection_kind_name(DnsmasqSelectionKind k);  // "", "explicit", "adopted", "automatic"

struct DnsmasqDetection {
    bool hook_ok{false};
    HookInspectResult inspect;
    std::vector<DnsmasqInstance> instances;
    std::optional<std::size_t> selected;        // index into instances
    DnsmasqSelectionKind selection{DnsmasqSelectionKind::None};
    DnsmasqErrorCode error{DnsmasqErrorCode::None};  // detection-level error
    std::string message;                        // human sentence for `error`
    std::vector<std::string> warnings;

    const DnsmasqInstance* selected_instance() const {
        return selected ? &instances[*selected] : nullptr;
    }
};

} // namespace keen_pbr3
