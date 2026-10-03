#pragma once

// Functional kernel probes.  Each probe exercises one netfilter primitive over
// a real (or injected) netlink transport and classifies the kernel's answer;
// none of them touches user traffic or leaves state behind.  They replace
// version guessing: a feature is "supported" only when the kernel accepted it.

#include "nl_msg.hpp"
#include "set_writer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace keen_pbr3::nfnl {

enum class ProbeStatus : uint8_t {
    not_run,      // the probe was never attempted
    ok,           // the kernel accepted the primitive
    unsupported,  // the kernel definitively rejected it (EINVAL/EOPNOTSUPP/...)
    error,        // inconclusive or failed for another reason (timeout, EPERM, busy)
    skipped,      // nothing to test against (e.g. no dynamic set exists yet)
};

const char* probe_status_name(ProbeStatus status);

struct ProbeResult {
    ProbeStatus status{ProbeStatus::not_run};
    std::string reason;

    bool is_ok() const { return status == ProbeStatus::ok; }
    // True for outcomes that must not be relied upon: unsupported and error.
    // not_run and skipped never block.
    bool blocks() const {
        return status == ProbeStatus::unsupported || status == ProbeStatus::error;
    }
};

ProbeResult make_probe_result(ProbeStatus status, std::string reason = {});

// Whether a kernel lets this process rewrite queued packets (NFQA_PAYLOAD in a
// verdict).  `unknown` means the kernel offers no way to find out (pre-4.9
// without NS_GET_USERNS) and the namespace is not the initial one.
enum class ReplacementCapability : uint8_t { unknown, supported, unsupported };
const char* replacement_capability_name(ReplacementCapability capability);

// Maps an errno from a bind/config/set operation to a probe result.  `what`
// prefixes the reason.  0 is ok; ETIMEDOUT is an error; EINVAL, EOPNOTSUPP,
// ENOSYS, ENOPROTOOPT and EPROTONOSUPPORT mean the kernel lacks the feature.
ProbeResult classify_errno(int err, std::string_view what);

// NFQA_CFG_FLAGS(FAIL_OPEN) probe.  `enable_err` is the ACK errno for the real
// request; `control_err` the ACK errno for the same request carrying an unknown
// flag bit.  Kernels older than 3.6 silently ignore the attribute (both ACK
// with 0), so a control that is accepted proves the flag was never parsed.
ProbeResult classify_fail_open(int enable_err, int control_err);

// --- builders (exposed for golden tests) ---------------------------------------
// IPSET_CMD_PROTOCOL request (answered with the kernel's IPSET_PROTOCOL).
void build_ipset_protocol(MsgBuilder& b, uint32_t seq);
// IPSET_CMD_DEL of one host address.
void build_ipset_del(MsgBuilder& b, uint32_t seq, const SetAdd& element);
// NFQNL_MSG_CONFIG carrying only NFQA_CFG_FLAGS/MASK.
void build_nfqueue_flags(MsgBuilder& b, uint32_t seq, uint16_t queue_num, uint32_t flags,
                         uint32_t mask);

// --- probes ---------------------------------------------------------------------
constexpr int kDefaultProbeTimeoutMs = 500;

struct IpsetProtocolProbe {
    ProbeResult result;
    int protocol{0};  // kernel IPSET_PROTOCOL, 0 when unknown
};
// Asks the kernel for its ipset protocol; requires >= IPSET_PROTOCOL_MIN (6).
IpsetProtocolProbe probe_ipset_protocol(SetWriterTransport& transport,
                                        int timeout_ms = kDefaultProbeTimeoutMs);

// nf_tables is present and usable when a batched NEWSETELEM against a set that
// does not exist fails with ENOENT (as opposed to EOPNOTSUPP/EINVAL).
ProbeResult probe_nft_tables(SetWriterTransport& transport, std::string_view table,
                             int timeout_ms = kDefaultProbeTimeoutMs);

// Adds `element` (with its timeout) to the named set and removes it again.  The
// add must be accepted; a failed or already-expired delete is not an error
// because the element carries a short timeout of its own.
ProbeResult probe_set_write(SetWriterTransport& transport, bool nft_backend,
                            std::string_view nft_table, const SetAdd& element,
                            int timeout_ms = kDefaultProbeTimeoutMs);

// Starts a ctnetlink dump and abandons it after the first reply.
ProbeResult probe_ctnetlink(SetWriterTransport& transport,
                            int timeout_ms = kDefaultProbeTimeoutMs);

// Production transport for the probes above (throws NlSocketError on socket
// failure).
std::unique_ptr<SetWriterTransport> make_probe_transport();

} // namespace keen_pbr3::nfnl
