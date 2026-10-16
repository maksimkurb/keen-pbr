#include "dnsmasq_types.hpp"

#include <arpa/inet.h>

namespace keen_pbr3 {

const char* dnsmasq_error_code_name(DnsmasqErrorCode code) {
    switch (code) {
        case DnsmasqErrorCode::None: return "";
        case DnsmasqErrorCode::NoHook: return "DNSMASQ_NO_HOOK";
        case DnsmasqErrorCode::HookFailed: return "DNSMASQ_HOOK_FAILED";
        case DnsmasqErrorCode::NotInstalled: return "DNSMASQ_NOT_INSTALLED";
        case DnsmasqErrorCode::NoDnsInstance: return "DNSMASQ_NO_DNS_INSTANCE";
        case DnsmasqErrorCode::InstanceNotFound: return "DNSMASQ_INSTANCE_NOT_FOUND";
        case DnsmasqErrorCode::InstanceAmbiguous: return "DNSMASQ_INSTANCE_AMBIGUOUS";
        case DnsmasqErrorCode::NotRunning: return "DNSMASQ_NOT_RUNNING";
        case DnsmasqErrorCode::DnsDisabled: return "DNSMASQ_DNS_DISABLED";
        case DnsmasqErrorCode::OtherNetns: return "DNSMASQ_OTHER_NETNS";
        case DnsmasqErrorCode::NoDnsListener: return "DNSMASQ_NO_DNS_LISTENER";
        case DnsmasqErrorCode::EndpointUnresponsive: return "DNSMASQ_ENDPOINT_UNRESPONSIVE";
        case DnsmasqErrorCode::DropinNotLoaded: return "DNSMASQ_DROPIN_NOT_LOADED";
        case DnsmasqErrorCode::ForeignDropin: return "DNSMASQ_FOREIGN_DROPIN";
        case DnsmasqErrorCode::ConfScriptUnsupported: return "DNSMASQ_CONF_SCRIPT_UNSUPPORTED";
        case DnsmasqErrorCode::DnsmasqTestFailed: return "DNSMASQ_TEST_FAILED";
        case DnsmasqErrorCode::PreflightFailed: return "DNSMASQ_PREFLIGHT_FAILED";
        case DnsmasqErrorCode::ApplyFailed: return "DNSMASQ_APPLY_FAILED";
        case DnsmasqErrorCode::ConfigNotLoaded: return "DNSMASQ_CONFIG_NOT_LOADED";
        case DnsmasqErrorCode::StampMismatch: return "DNSMASQ_STAMP_MISMATCH";
        case DnsmasqErrorCode::RepairPaused: return "DNSMASQ_REPAIR_PAUSED";
        case DnsmasqErrorCode::ProbeFailed: return "DNSMASQ_PROBE_FAILED";
    }
    return "";
}

std::string DnsmasqEndpoint::str() const {
    if (address.find(':') != std::string::npos) {
        return "[" + address + "]:" + std::to_string(port);
    }
    return address + ":" + std::to_string(port);
}

namespace {

bool valid_ip(const std::string& addr) {
    unsigned char buf[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, addr.c_str(), buf) == 1 ||
           inet_pton(AF_INET6, addr.c_str(), buf) == 1;
}

bool parse_port(std::string_view s, std::uint16_t& out) {
    if (s.empty() || s.size() > 5) return false;
    unsigned v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v < 1 || v > 65535) return false;
    out = static_cast<std::uint16_t>(v);
    return true;
}

} // namespace

std::optional<DnsmasqEndpoint> parse_dnsmasq_endpoint(std::string_view text) {
    std::string addr;
    std::uint16_t port = 53;
    if (!text.empty() && text.front() == '[') {
        const auto close = text.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        addr = std::string(text.substr(1, close - 1));
        const auto rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':' || !parse_port(rest.substr(1), port)) return std::nullopt;
        }
    } else {
        std::size_t colons = 0;
        for (char c : text) colons += (c == ':');
        if (colons == 1) {
            const auto pos = text.find(':');
            addr = std::string(text.substr(0, pos));
            if (!parse_port(text.substr(pos + 1), port)) return std::nullopt;
        } else {
            addr = std::string(text);
        }
    }
    if (addr.empty() || !valid_ip(addr)) return std::nullopt;
    return DnsmasqEndpoint{addr, port};
}

const char* endpoint_evidence_name(EndpointEvidence e) {
    switch (e) {
        case EndpointEvidence::None: return "none";
        case EndpointEvidence::SocketOwned: return "socket_owned";
        case EndpointEvidence::Responding: return "responding";
        case EndpointEvidence::StampVerified: return "stamp_verified";
    }
    return "none";
}

const char* dropin_state_name(DropinState s) {
    switch (s) {
        case DropinState::Absent: return "absent";
        case DropinState::Current: return "current";
        case DropinState::Foreign: return "foreign";
    }
    return "absent";
}

std::optional<DropinState> parse_dropin_state(std::string_view s) {
    if (s == "absent") return DropinState::Absent;
    if (s == "current") return DropinState::Current;
    if (s == "foreign") return DropinState::Foreign;
    return std::nullopt;
}

const char* selection_kind_name(DnsmasqSelectionKind k) {
    switch (k) {
        case DnsmasqSelectionKind::None: return "";
        case DnsmasqSelectionKind::Explicit: return "explicit";
        case DnsmasqSelectionKind::Adopted: return "adopted";
        case DnsmasqSelectionKind::Automatic: return "automatic";
    }
    return "";
}

} // namespace keen_pbr3
