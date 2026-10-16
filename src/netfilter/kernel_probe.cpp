#include "kernel_probe.hpp"

#include "conntrack.hpp"
#include "uapi_compat.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace keen_pbr3::nfnl {

namespace {

constexpr uint16_t kReqAck = NLM_F_REQUEST | NLM_F_ACK;
constexpr uint16_t kMsgQueueConfig = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG;
constexpr uint16_t kMsgIpsetProtocol = (NFNL_SUBSYS_IPSET << 8) | IPSET_CMD_PROTOCOL;
// Beyond any flag the kernel defines (NFQA_CFG_F_MAX is single digits).
constexpr uint32_t kUnknownQueueFlag = 0x80000000u;

bool is_unsupported_errno(int err) {
    return err == EINVAL || err == EOPNOTSUPP || err == ENOSYS || err == ENOPROTOOPT ||
           err == EPROTONOSUPPORT || err == ENOTTY;
}

std::string with_errno(std::string_view what, int err) {
    std::string out(what);
    out += ": ";
    out += std::strerror(err);
    return out;
}

// Sends one request and returns the transport result (first error, 0, or
// ETIMEDOUT).  `on_msg` may be empty.
int run_single(SetWriterTransport& transport, const MsgBuilder& request, uint32_t seq,
               int timeout_ms, const std::function<void(const MsgView&)>& on_msg) {
    return transport.transact(request.data(), request.size(), seq, seq, timeout_ms, on_msg,
                              [](uint32_t, int) {});
}

void put_ipset_element(MsgBuilder& b, const SetAdd& a, bool with_timeout) {
    b.put_u8(IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL_MIN);
    b.put_strz(IPSET_ATTR_SETNAME, a.set_name);
    const std::size_t data = b.nest_begin(IPSET_ATTR_DATA);
    const std::size_t ip = b.nest_begin(IPSET_ATTR_IP);
    if (a.family == 6) {
        b.put(IPSET_ATTR_IPADDR_IPV6 | NLA_F_NET_BYTEORDER, a.addr.data(), 16);
    } else {
        b.put(IPSET_ATTR_IPADDR_IPV4 | NLA_F_NET_BYTEORDER, a.addr.data(), 4);
    }
    b.nest_end(ip);
    if (with_timeout) b.put_u32_be(IPSET_ATTR_TIMEOUT | NLA_F_NET_BYTEORDER, a.timeout_s);
    b.nest_end(data);
}

} // namespace

const char* probe_status_name(ProbeStatus status) {
    switch (status) {
    case ProbeStatus::not_run: return "not_run";
    case ProbeStatus::ok: return "ok";
    case ProbeStatus::unsupported: return "unsupported";
    case ProbeStatus::error: return "error";
    case ProbeStatus::skipped: return "skipped";
    }
    return "not_run";
}

ProbeResult make_probe_result(ProbeStatus status, std::string reason) {
    ProbeResult result;
    result.status = status;
    result.reason = std::move(reason);
    return result;
}

const char* replacement_capability_name(ReplacementCapability capability) {
    switch (capability) {
    case ReplacementCapability::unknown: return "unknown";
    case ReplacementCapability::supported: return "supported";
    case ReplacementCapability::unsupported: return "unsupported";
    }
    return "unknown";
}

ProbeResult classify_errno(int err, std::string_view what) {
    if (err == 0) return make_probe_result(ProbeStatus::ok);
    if (err == ETIMEDOUT) {
        return make_probe_result(ProbeStatus::error, std::string(what) + ": timed out");
    }
    if (is_unsupported_errno(err)) {
        return make_probe_result(ProbeStatus::unsupported, with_errno(what, err));
    }
    return make_probe_result(ProbeStatus::error, with_errno(what, err));
}

std::string nfnl_module_missing_hint(bool queue, int err, std::string_view proc_path) {
    if (err != EINVAL) return {};
    const std::string path = proc_path.empty()
        ? std::string(queue ? "/proc/net/netfilter/nfnetlink_queue"
                            : "/proc/net/netfilter/nfnetlink_log")
        : std::string(proc_path);
    if (::access(path.c_str(), F_OK) == 0) return {};
    return queue
        ? "kernel module nfnetlink_queue is not available (OpenWrt: install package "
          "kmod-nfnetlink-queue and kmod-nft-queue)"
        : "kernel module nfnetlink_log is not available (OpenWrt: install package "
          "kmod-nfnetlink-log)";
}

ProbeResult classify_fail_open(int enable_err, int control_err) {
    if (enable_err != 0) {
        ProbeResult result = classify_errno(enable_err, "NFQA_CFG_F_FAIL_OPEN");
        if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
        return result;
    }
    if (control_err == ETIMEDOUT) {
        return make_probe_result(ProbeStatus::error,
                                 "NFQA_CFG_F_FAIL_OPEN: control request timed out");
    }
    if (control_err == 0) {
        return make_probe_result(
            ProbeStatus::unsupported,
            "kernel ignores NFQA_CFG_FLAGS (accepted an unknown flag); fail-open unavailable");
    }
    return make_probe_result(ProbeStatus::ok);
}

void build_ipset_protocol(MsgBuilder& b, uint32_t seq) {
    b.begin(kMsgIpsetProtocol, kReqAck, seq, AF_UNSPEC, 0);
    b.put_u8(IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL_MIN);
    b.end();
}

void build_ipset_del(MsgBuilder& b, uint32_t seq, const SetAdd& element) {
    b.begin(static_cast<uint16_t>((NFNL_SUBSYS_IPSET << 8) | IPSET_CMD_DEL), kReqAck, seq,
            NFPROTO_IPV4, 0);
    put_ipset_element(b, element, /*with_timeout=*/false);
    b.end();
}

void build_nfqueue_flags(MsgBuilder& b, uint32_t seq, uint16_t queue_num, uint32_t flags,
                         uint32_t mask) {
    b.begin(kMsgQueueConfig, kReqAck, seq, AF_UNSPEC, queue_num);
    b.put_u32_be(uapi::kNfqaCfgMask, mask);
    b.put_u32_be(uapi::kNfqaCfgFlags, flags);
    b.end();
}

void build_nft_getsetelem(MsgBuilder& b, uint32_t seq, std::string_view table,
                          const SetAdd& element) {
    b.begin(static_cast<uint16_t>((NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_GETSETELEM), kReqAck, seq,
            NFPROTO_INET, 0);
    b.put_strz(NFTA_SET_ELEM_LIST_TABLE, table);
    b.put_strz(NFTA_SET_ELEM_LIST_SET, element.set_name);
    const std::size_t elems = b.nest_begin(NFTA_SET_ELEM_LIST_ELEMENTS);
    const std::size_t elem = b.nest_begin(NFTA_LIST_ELEM);
    const std::size_t key = b.nest_begin(NFTA_SET_ELEM_KEY);
    b.put(NFTA_DATA_VALUE, element.addr.data(), element.family == 6 ? 16 : 4);
    b.nest_end(key);
    b.nest_end(elem);
    b.nest_end(elems);
    b.end();
}

int64_t parse_nft_setelem_expiration_ms(const MsgView& message) {
    int64_t result = -1;
    if (message.type != ((NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWSETELEM)) return result;
    (void)for_each_attr(message.attrs, [&](const Attr& list_attr) {
        if ((list_attr.type & NLA_TYPE_MASK) != NFTA_SET_ELEM_LIST_ELEMENTS) return true;
        return for_each_attr(list_attr.payload, [&](const Attr& elem_attr) {
            if ((elem_attr.type & NLA_TYPE_MASK) != NFTA_LIST_ELEM) return true;
            (void)for_each_attr(elem_attr.payload, [&](const Attr& field) {
                if ((field.type & NLA_TYPE_MASK) == NFTA_SET_ELEM_EXPIRATION &&
                    field.payload.size() >= 8) {
                    result = static_cast<int64_t>(attr_u64_be(field));
                    return false;
                }
                return true;
            });
            return false;  // only the first element matters
        });
    });
    return result;
}

IpsetProtocolProbe probe_ipset_protocol(SetWriterTransport& transport, int timeout_ms) {
    IpsetProtocolProbe out;
    MsgBuilder request(256);
    const uint32_t seq = transport.next_seq();
    build_ipset_protocol(request, seq);
    int protocol = 0;
    const int rc = run_single(transport, request, seq, timeout_ms, [&](const MsgView& m) {
        if (m.type != kMsgIpsetProtocol) return;
        (void)for_each_attr(m.attrs, [&](const Attr& attr) {
            if ((attr.type & NLA_TYPE_MASK) == IPSET_ATTR_PROTOCOL) {
                protocol = attr_u8(attr);
                return false;
            }
            return true;
        });
    });
    if (rc != 0) {
        out.result = classify_errno(rc, "ipset protocol query");
        if (out.result.status == ProbeStatus::ok) out.result.status = ProbeStatus::error;
        return out;
    }
    if (protocol == 0) {
        out.result = make_probe_result(ProbeStatus::error,
                                       "ipset protocol query: kernel reply carried no protocol");
        return out;
    }
    out.protocol = protocol;
    if (protocol < IPSET_PROTOCOL_MIN) {
        out.result = make_probe_result(
            ProbeStatus::unsupported,
            "kernel ipset protocol " + std::to_string(protocol) + " is older than the required " +
                std::to_string(IPSET_PROTOCOL_MIN));
        return out;
    }
    out.result = make_probe_result(ProbeStatus::ok, "ipset protocol " + std::to_string(protocol));
    return out;
}

ProbeResult probe_nft_tables(SetWriterTransport& transport, std::string_view table,
                             int timeout_ms) {
    SetAdd element;
    element.set_name = "kpbr_probe_noset";
    element.family = 4;
    element.addr = {192, 0, 2, 255};
    element.timeout_s = 1;
    MsgBuilder request(512);
    request.batch_begin(transport.next_seq());
    const uint32_t seq = transport.next_seq();
    build_nft_newsetelem(request, seq, table, element, /*exclusive=*/false);
    request.batch_end(transport.next_seq());
    const int rc = run_single(transport, request, seq, timeout_ms, nullptr);
    // The set does not exist: ENOENT proves the kernel parsed the nf_tables
    // batch and looked the table/set up.
    if (rc == 0 || rc == ENOENT) {
        return make_probe_result(ProbeStatus::ok, "nf_tables netlink available");
    }
    ProbeResult result = classify_errno(rc, "nf_tables netlink");
    if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
    return result;
}

ProbeResult probe_set_write(SetWriterTransport& transport, bool nft_backend,
                            std::string_view nft_table, const SetAdd& element, int timeout_ms) {
    MsgBuilder request(512);
    const uint32_t add_seq = [&] {
        if (nft_backend) request.batch_begin(transport.next_seq());
        const uint32_t seq = transport.next_seq();
        if (nft_backend) {
            build_nft_newsetelem(request, seq, nft_table, element, /*exclusive=*/false);
            request.batch_end(transport.next_seq());
        } else {
            build_ipset_add(request, seq, element, /*exclusive=*/false);
        }
        return seq;
    }();
    const int add_rc = run_single(transport, request, add_seq, timeout_ms, nullptr);
    if (add_rc != 0) {
        ProbeResult result = classify_errno(
            add_rc, std::string("test add to dynamic set '") + std::string(element.set_name) + "'");
        if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
        return result;
    }

    request.clear();
    uint32_t del_seq = 0;
    if (nft_backend) {
        request.batch_begin(transport.next_seq());
        del_seq = transport.next_seq();
        build_nft_delsetelem(request, del_seq, nft_table, element);
        request.batch_end(transport.next_seq());
    } else {
        del_seq = transport.next_seq();
        build_ipset_del(request, del_seq, element);
    }
    const int del_rc = run_single(transport, request, del_seq, timeout_ms, nullptr);
    if (del_rc != 0 && del_rc != ENOENT && del_rc != IPSET_ERR_EXIST) {
        return make_probe_result(ProbeStatus::ok,
                                 "element written; cleanup delete failed (" +
                                     std::string(std::strerror(del_rc)) +
                                     "), it expires after " + std::to_string(element.timeout_s) +
                                     "s");
    }
    return make_probe_result(ProbeStatus::ok, "element add/delete accepted");
}

ProbeResult probe_nft_timeout_update(SetWriterTransport& transport, std::string_view nft_table,
                                     const SetAdd& element, int timeout_ms) {
    const std::string what = "nft timeout update in set '" + std::string(element.set_name) + "'";
    const auto batched = [&](const std::function<void(MsgBuilder&, uint32_t)>& build) {
        MsgBuilder request(512);
        request.batch_begin(transport.next_seq());
        const uint32_t seq = transport.next_seq();
        build(request, seq);
        request.batch_end(transport.next_seq());
        return run_single(transport, request, seq, timeout_ms, nullptr);
    };
    const auto remove = [&] {
        return batched([&](MsgBuilder& b, uint32_t seq) {
            build_nft_delsetelem(b, seq, nft_table, element);
        });
    };

    // A leftover with a long timeout would make an old kernel look capable.
    (void)remove();

    SetAdd probe = element;
    probe.timeout_s = 5;
    int rc = batched([&](MsgBuilder& b, uint32_t seq) {
        build_nft_newsetelem(b, seq, nft_table, probe, /*exclusive=*/false);
    });
    if (rc != 0) {
        ProbeResult result = classify_errno(rc, what + ": add");
        if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
        return result;
    }

    ProbeResult result;
    probe.timeout_s = 300;
    rc = batched([&](MsgBuilder& b, uint32_t seq) {
        build_nft_refresh_setelem(b, seq, nft_table, probe);
    });
    if (rc != 0) {
        result = classify_errno(rc, what + ": refresh");
        if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
    } else {
        MsgBuilder request(512);
        const uint32_t seq = transport.next_seq();
        build_nft_getsetelem(request, seq, nft_table, probe);
        int64_t expiration_ms = -1;
        rc = run_single(transport, request, seq, timeout_ms, [&](const MsgView& m) {
            const int64_t value = parse_nft_setelem_expiration_ms(m);
            if (value >= 0) expiration_ms = value;
        });
        if (rc != 0) {
            result = classify_errno(rc, what + ": read back");
            if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
            // The kernel not knowing GETSETELEM says nothing about the update.
            if (result.status == ProbeStatus::unsupported) result.status = ProbeStatus::error;
        } else if (expiration_ms < 0) {
            result = make_probe_result(ProbeStatus::error,
                                       what + ": read back carried no expiration");
        } else if (expiration_ms > 10000) {
            result = make_probe_result(ProbeStatus::ok,
                                       "existing element timeout extended in place (remaining " +
                                           std::to_string(expiration_ms) + " ms)");
        } else {
            result = make_probe_result(
                ProbeStatus::unsupported,
                "kernel keeps the old expiration of an existing element (remaining " +
                    std::to_string(expiration_ms) +
                    " ms after refresh to 300 s); using delete+add refresh");
        }
    }

    const int del_rc = remove();
    if (del_rc != 0 && del_rc != ENOENT && result.status == ProbeStatus::ok) {
        result.reason += "; cleanup delete failed (" + std::string(std::strerror(del_rc)) +
                         "), probe element expires on its own";
    }
    return result;
}

ProbeResult probe_ctnetlink(SetWriterTransport& transport, int timeout_ms) {
    MsgBuilder request(128);
    const uint32_t seq = transport.next_seq();
    build_conntrack_get(request, seq, ConntrackFamily::ipv4);
    bool saw_reply = false;
    const int rc = run_single(transport, request, seq, timeout_ms,
                              [&](const MsgView&) { saw_reply = true; });
    // A large table keeps the dump running past the deadline; any reply data
    // already proves ctnetlink answers.  The abandoned dump is cancelled when
    // the socket closes.
    if (rc == 0 || (rc == ETIMEDOUT && saw_reply)) {
        return make_probe_result(ProbeStatus::ok, "ctnetlink dump answered");
    }
    ProbeResult result = classify_errno(rc, "ctnetlink dump");
    if (result.status == ProbeStatus::ok) result.status = ProbeStatus::error;
    return result;
}

std::unique_ptr<SetWriterTransport> make_probe_transport() {
    return make_netlink_transport(1 << 18);
}

} // namespace keen_pbr3::nfnl
