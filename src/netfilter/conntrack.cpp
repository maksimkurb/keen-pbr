#include "conntrack.hpp"
#include "uapi_compat.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>

namespace keen_pbr3::nfnl {
namespace {

constexpr uint16_t kCtGet = static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) |
                                                  IPCTNL_MSG_CT_GET);
constexpr uint16_t kCtDelete = static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) |
                                                     IPCTNL_MSG_CT_DELETE);
constexpr uint16_t kCtNew = static_cast<uint16_t>((NFNL_SUBSYS_CTNETLINK << 8) |
                                                  IPCTNL_MSG_CT_NEW);
constexpr uint16_t kAckFlags = NLM_F_REQUEST | NLM_F_ACK;
constexpr std::size_t kRxBufSize = std::size_t{256} * 1024U;

uint8_t netlink_family(ConntrackFamily family) {
    return family == ConntrackFamily::ipv4 ? AF_INET : AF_INET6;
}

bool parse_ip_tuple(ByteView attrs, ConntrackTuple& tuple) {
    Attr table[CTA_IP_MAX + 1]{};
    if (!parse_attrs(attrs, table, CTA_IP_MAX + 1)) return false;
    const Attr& v4_src = table[CTA_IP_V4_SRC];
    const Attr& v4_dst = table[CTA_IP_V4_DST];
    const Attr& v6_src = table[CTA_IP_V6_SRC];
    const Attr& v6_dst = table[CTA_IP_V6_DST];
    if (v4_src.payload.size() == 4 && v4_dst.payload.size() == 4) {
        tuple.family = ConntrackFamily::ipv4;
        std::memcpy(tuple.src.data(), v4_src.payload.data(), 4);
        std::memcpy(tuple.dst.data(), v4_dst.payload.data(), 4);
        return true;
    }
    if (v6_src.payload.size() == 16 && v6_dst.payload.size() == 16) {
        tuple.family = ConntrackFamily::ipv6;
        std::memcpy(tuple.src.data(), v6_src.payload.data(), 16);
        std::memcpy(tuple.dst.data(), v6_dst.payload.data(), 16);
        return true;
    }
    return false;
}

bool parse_proto_tuple(ByteView attrs, ConntrackTuple& tuple) {
    Attr table[CTA_PROTO_MAX + 1]{};
    if (!parse_attrs(attrs, table, CTA_PROTO_MAX + 1)) return false;
    if (table[CTA_PROTO_NUM].payload.size() != 1) return false;
    tuple.protocol = table[CTA_PROTO_NUM].payload.data()[0];
    const bool have_src = table[CTA_PROTO_SRC_PORT].payload.size() != 0;
    const bool have_dst = table[CTA_PROTO_DST_PORT].payload.size() != 0;
    if (have_src != have_dst) return false;
    tuple.has_ports = have_src;
    if (have_src) {
        if (table[CTA_PROTO_SRC_PORT].payload.size() != sizeof(uint16_t) ||
            table[CTA_PROTO_DST_PORT].payload.size() != sizeof(uint16_t)) return false;
        tuple.src_port = attr_u16_be(table[CTA_PROTO_SRC_PORT]);
        tuple.dst_port = attr_u16_be(table[CTA_PROTO_DST_PORT]);
        tuple.has_protocol_key = true;
        return true;
    }
    const bool is_icmp4 = tuple.protocol == IPPROTO_ICMP;
    const bool is_icmp6 = tuple.protocol == IPPROTO_ICMPV6;
    const uint16_t id_type = is_icmp6 ? CTA_PROTO_ICMPV6_ID : CTA_PROTO_ICMP_ID;
    const uint16_t type_type = is_icmp6 ? CTA_PROTO_ICMPV6_TYPE : CTA_PROTO_ICMP_TYPE;
    const uint16_t code_type = is_icmp6 ? CTA_PROTO_ICMPV6_CODE : CTA_PROTO_ICMP_CODE;
    if (is_icmp4 || is_icmp6) {
        if (table[id_type].payload.size() != sizeof(uint16_t) ||
            table[type_type].payload.size() != sizeof(uint8_t) ||
            table[code_type].payload.size() != sizeof(uint8_t)) return false;
        tuple.icmp_id = attr_u16_be(table[id_type]);
        tuple.icmp_type = attr_u8(table[type_type]);
        tuple.icmp_code = attr_u8(table[code_type]);
        tuple.has_icmp_key = true;
        tuple.has_protocol_key = true;
    } else {
        // Unknown L4 protocols may be present in a dump, but without their
        // protocol-specific key they are not safe deletion candidates.
        tuple.has_protocol_key = false;
    }
    return true;
}

bool parse_tuple_payload(ByteView attrs, ConntrackTuple& tuple) {
    Attr tuple_attrs[CTA_TUPLE_MAX + 1]{};
    if (!parse_attrs(attrs, tuple_attrs, CTA_TUPLE_MAX + 1)) return false;
    if (!parse_ip_tuple(tuple_attrs[CTA_TUPLE_IP].payload, tuple)) return false;
    return parse_proto_tuple(tuple_attrs[CTA_TUPLE_PROTO].payload, tuple);
}

bool parse_zone(const Attr& attr, uint16_t& zone, bool& present) {
    if (attr.payload.size() == 0) return true;
    if (attr.payload.size() != sizeof(uint16_t)) return false;
    zone = attr_u16_be(attr);
    present = true;
    return true;
}

bool parse_original_tuple(ByteView attrs, ConntrackTuple& tuple,
                          bool& deletion_safe) {
    Attr top[CTA_MAX + 1]{};
    if (!parse_attrs(attrs, top, CTA_MAX + 1)) return false;
    const Attr& orig = top[CTA_TUPLE_ORIG];
    if (orig.payload.size() == 0) return false;
    Attr tuple_attrs[CTA_TUPLE_MAX + 1]{};
    if (!parse_attrs(orig.payload, tuple_attrs, CTA_TUPLE_MAX + 1)) return false;
    if (!parse_tuple_payload(orig.payload, tuple)) return false;
    uint16_t tuple_zone = 0;
    bool have_tuple_zone = false;
    if (!parse_zone(tuple_attrs[uapi::kCtaTupleZone], tuple_zone, have_tuple_zone)) return false;
    uint16_t top_zone = 0;
    bool have_top_zone = false;
    if (!parse_zone(top[uapi::kCtaZone], top_zone, have_top_zone)) return false;

    uint16_t reply_zone = 0;
    bool have_reply_zone = false;
    if (top[CTA_TUPLE_REPLY].payload.size() != 0) {
        Attr reply_attrs[CTA_TUPLE_MAX + 1]{};
        if (!parse_attrs(top[CTA_TUPLE_REPLY].payload, reply_attrs,
                         CTA_TUPLE_MAX + 1)) return false;
        if (!parse_zone(reply_attrs[uapi::kCtaTupleZone], reply_zone, have_reply_zone)) {
            return false;
        }
    }

    const unsigned locations = static_cast<unsigned>(have_top_zone) +
                               static_cast<unsigned>(have_tuple_zone) +
                               static_cast<unsigned>(have_reply_zone);
    if (locations != 0) {
        // Kernel dumps use exactly one canonical location.  If an old or
        // synthetic producer sends duplicates, retain the value but make the
        // entry non-deletable rather than aborting the entire dump.
        if (locations > 1) deletion_safe = false;
        if (have_top_zone) {
            tuple.zone = top_zone;
            tuple.zone_scope = ConntrackZoneScope::top;
        } else if (have_tuple_zone) {
            tuple.zone = tuple_zone;
            tuple.zone_scope = ConntrackZoneScope::original;
        } else {
            tuple.zone = reply_zone;
            tuple.zone_scope = ConntrackZoneScope::reply;
        }
        tuple.has_zone = true;
    }
    return true;
}

void put_tuple(MsgBuilder& builder, uint16_t tuple_type, const ConntrackTuple& tuple,
               bool include_zone) {
    const std::size_t orig = builder.nest_begin(tuple_type);
    const std::size_t ip = builder.nest_begin(CTA_TUPLE_IP);
    if (tuple.family == ConntrackFamily::ipv4) {
        builder.put(CTA_IP_V4_SRC, tuple.src.data(), 4);
        builder.put(CTA_IP_V4_DST, tuple.dst.data(), 4);
    } else {
        builder.put(CTA_IP_V6_SRC, tuple.src.data(), 16);
        builder.put(CTA_IP_V6_DST, tuple.dst.data(), 16);
    }
    builder.nest_end(ip);
    const std::size_t proto = builder.nest_begin(CTA_TUPLE_PROTO);
    builder.put_u8(CTA_PROTO_NUM, tuple.protocol);
    if (tuple.has_ports) {
        builder.put_u16_be(CTA_PROTO_SRC_PORT, tuple.src_port);
        builder.put_u16_be(CTA_PROTO_DST_PORT, tuple.dst_port);
    } else if (tuple.has_icmp_key) {
        const bool is_icmp6 = tuple.protocol == IPPROTO_ICMPV6;
        builder.put_u16_be(is_icmp6 ? CTA_PROTO_ICMPV6_ID : CTA_PROTO_ICMP_ID,
                           tuple.icmp_id);
        builder.put_u8(is_icmp6 ? CTA_PROTO_ICMPV6_TYPE : CTA_PROTO_ICMP_TYPE,
                       tuple.icmp_type);
        builder.put_u8(is_icmp6 ? CTA_PROTO_ICMPV6_CODE : CTA_PROTO_ICMP_CODE,
                       tuple.icmp_code);
    }
    builder.nest_end(proto);
    // For CT_DELETE the kernel accepts a directional zone only inside the
    // tuple selected by the request.  A both-direction zone is emitted by the
    // caller at CTA_ZONE; never emit both locations.
    if (include_zone && tuple.has_zone) {
        builder.put_u16_be(uapi::kCtaTupleZone, tuple.zone);
    }
    builder.nest_end(orig);
}

int poll_readable(int fd, int timeout_ms) {
    pollfd pfd{fd, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(timeout_ms, 0));
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int rc = ::poll(&pfd, 1, static_cast<int>(std::max<int64_t>(left, 0)));
        if (rc < 0 && errno == EINTR) {
            if (std::chrono::steady_clock::now() >= deadline) return 0;
            continue;
        }
        return rc;
    }
}

} // namespace

bool ConntrackTuple::operator==(const ConntrackTuple& other) const {
    const std::size_t address_size = family == ConntrackFamily::ipv4 ? 4 : 16;
    return family == other.family && protocol == other.protocol &&
           std::equal(src.begin(), src.begin() + address_size, other.src.begin()) &&
           std::equal(dst.begin(), dst.begin() + address_size, other.dst.begin()) &&
           src_port == other.src_port && dst_port == other.dst_port &&
           has_ports == other.has_ports && icmp_id == other.icmp_id &&
           icmp_type == other.icmp_type && icmp_code == other.icmp_code &&
           has_icmp_key == other.has_icmp_key && has_protocol_key == other.has_protocol_key &&
           zone == other.zone && has_zone == other.has_zone && zone_scope == other.zone_scope;
}

bool parse_conntrack_entry(const MsgView& message, ConntrackEntry& entry) {
    if (message.type != kCtNew && message.type != kCtGet) return false;
    if (message.family != AF_INET && message.family != AF_INET6) return false;
    ConntrackEntry parsed;
    if (!parse_original_tuple(message.attrs, parsed.original, parsed.deletion_safe)) return false;
    if ((parsed.original.family == ConntrackFamily::ipv4 && message.family != AF_INET) ||
        (parsed.original.family == ConntrackFamily::ipv6 && message.family != AF_INET6)) {
        return false;
    }
    Attr table[CTA_MAX + 1]{};
    if (!parse_attrs(message.attrs, table, CTA_MAX + 1)) return false;
    if (parsed.original.zone_scope == ConntrackZoneScope::reply) {
        ConntrackTuple reply;
        if (table[CTA_TUPLE_REPLY].payload.size() == 0 ||
            !parse_tuple_payload(table[CTA_TUPLE_REPLY].payload, reply) ||
            reply.family != parsed.original.family) {
            // Keep the original tuple in the stream, but do not expose an
            // unsafe deletion candidate when the reply key is incomplete or
            // belongs to a different family than the original tuple.
            parsed.deletion_safe = false;
        } else {
            reply.zone = parsed.original.zone;
            reply.has_zone = true;
            reply.zone_scope = ConntrackZoneScope::reply;
            parsed.reply = reply;
        }
    }
    if (table[CTA_MARK].payload.size() == sizeof(uint32_t)) {
        parsed.mark = attr_u32_be(table[CTA_MARK]);
    } else if (table[CTA_MARK].payload.size() != 0) {
        return false;
    }
    if (table[CTA_ID].payload.size() == sizeof(uint32_t)) {
        parsed.id = attr_u32_be(table[CTA_ID]);
        parsed.has_id = true;
    } else if (table[CTA_ID].payload.size() != 0) {
        return false;
    }
    entry = parsed;
    return true;
}

void build_conntrack_get(MsgBuilder& builder, uint32_t seq, ConntrackFamily family,
                         const ConntrackKernelFilter* kernel_filter) {
    builder.begin(kCtGet, NLM_F_REQUEST | NLM_F_DUMP, seq,
                  netlink_family(family), 0);
    if (kernel_filter) {
        // The kernel matches CTA_TUPLE_ORIG against the attributes named in
        // CTA_FILTER_ORIG_FLAGS; only the source address is selected.
        const std::size_t filter = builder.nest_begin(uapi::kCtaFilter);
        builder.put_u32_be(uapi::kCtaFilterOrigFlags, uapi::kCtaFilterFlagIpSrc);
        builder.nest_end(filter);
        const std::size_t orig = builder.nest_begin(CTA_TUPLE_ORIG);
        const std::size_t ip = builder.nest_begin(CTA_TUPLE_IP);
        if (family == ConntrackFamily::ipv4) {
            builder.put(CTA_IP_V4_SRC, kernel_filter->src.data(), 4);
        } else {
            builder.put(CTA_IP_V6_SRC, kernel_filter->src.data(), 16);
        }
        builder.nest_end(ip);
        builder.nest_end(orig);
    }
    builder.end();
}

bool conntrack_kernel_filter_refused(int error) {
    return error == EINVAL || error == EOPNOTSUPP || error == ENOSYS;
}

std::function<bool(const ConntrackEntry&)> make_client_destination_filter(
    ConntrackFamily family, const std::array<uint8_t, 16>& client,
    std::vector<std::array<uint8_t, 16>> dsts) {
    const std::size_t len = family == ConntrackFamily::ipv6 ? 16 : 4;
    return [family, len, client, dsts = std::move(dsts)](const ConntrackEntry& entry) {
        if (entry.original.family != family) return false;
        if (!std::equal(client.begin(), client.begin() + len, entry.original.src.begin())) {
            return false;
        }
        return std::any_of(dsts.begin(), dsts.end(), [&](const std::array<uint8_t, 16>& dst) {
            return std::equal(dst.begin(), dst.begin() + len, entry.original.dst.begin());
        });
    };
}

void build_conntrack_delete(MsgBuilder& builder, uint32_t seq,
                            const ConntrackEntry& entry) {
    if (!entry.deletion_candidate()) {
        throw NlSocketError("conntrack entry is not a safe tuple delete candidate");
    }
    builder.begin(kCtDelete, kAckFlags, seq,
                  netlink_family(entry.original.family), 0);
    const ConntrackZoneScope scope =
        entry.original.has_zone && entry.original.zone_scope == ConntrackZoneScope::none
            ? ConntrackZoneScope::top
            : entry.original.zone_scope;
    if (scope == ConntrackZoneScope::reply) {
        put_tuple(builder, CTA_TUPLE_REPLY, *entry.reply, true);
    } else {
        const bool nested_zone = scope == ConntrackZoneScope::original;
        put_tuple(builder, CTA_TUPLE_ORIG, entry.original, nested_zone);
    }
    if (entry.original.has_zone && scope == ConntrackZoneScope::top) {
        builder.put_u16_be(uapi::kCtaZone, entry.original.zone);
    }
    if (entry.has_id) builder.put_u32_be(CTA_ID, entry.id);
    builder.end();
}

ConntrackDump::ConntrackDump(ConntrackFamily family, const ConntrackOptions& options)
    : socket_(options.rcvbuf_bytes, true), rx_(kRxBufSize), max_entries_(options.max_entries),
      kernel_filter_(options.kernel_filter), filter_(options.filter) {
    seq_ = socket_.next_seq();
    MsgBuilder request;
    build_conntrack_get(request, seq_, family,
                        options.kernel_filter ? &*options.kernel_filter : nullptr);
    int error = 0;
    if (!socket_.send(request.data(), request.size(), error)) {
        throw NlSocketError(std::string("conntrack dump send: ") + std::strerror(error));
    }
}

int ConntrackDump::receive(int timeout_ms) {
    if (complete_) return 1;
    if (last_errno_ != 0) return -1;
    const int ready = poll_readable(socket_.fd(), timeout_ms);
    if (ready < 0) {
        last_errno_ = errno;
        return -1;
    }
    if (ready == 0) return 0;
    int recv_error = 0;
    const ssize_t length = socket_.recv(rx_, recv_error);
    if (length < 0) {
        last_errno_ = recv_error;
        return -1;
    }
    return process_datagram(ByteView(rx_.data(), static_cast<std::size_t>(length)));
}

int ConntrackDump::process_datagram(ByteView datagram) {
    if (complete_) return 1;
    if (last_errno_ != 0) return -1;
    bool malformed = false;
    const bool well_formed = for_each_msg(
        datagram,
        [&](const MsgView& message) {
            if (message.seq != seq_) return true;
            if (message.type == NLMSG_ERROR) {
                int error = 0;
                uint32_t original_seq = 0;
                if (!parse_error(message, error, original_seq)) {
                    last_errno_ = EPROTO;
                    malformed = true;
                    return true;
                }
                // A zero ACK is not completion for a dump.  Only a negative
                // ACK terminates it as a failed request.
                if (error != 0) last_errno_ = error;
                return true;
            }
            if (message.type == NLMSG_DONE) {
                int error = 0;
                if (!parse_done_error(message, error)) {
                    last_errno_ = EPROTO;
                    malformed = true;
                } else if (error != 0) {
                    last_errno_ = error;
                } else {
                    complete_ = true;
                }
                return true;
            }
            if (message.type != kCtNew && message.type != kCtGet) return true;
            ConntrackEntry entry;
            if (!parse_conntrack_entry(message, entry)) {
                last_errno_ = EPROTO;
                malformed = true;
                return true;
            }
            if (kernel_filter_) {
                const std::size_t len = entry.original.family == ConntrackFamily::ipv6 ? 16 : 4;
                if (!std::equal(kernel_filter_->src.begin(), kernel_filter_->src.begin() + len,
                                entry.original.src.begin())) {
                    ++kernel_mismatches_;
                }
            }
            if (filter_ && !filter_(entry)) return true;
            if (entries_.size() >= max_entries_) {
                last_errno_ = E2BIG;
                return true;
            }
            entries_.push_back(entry);
            return true;
        });
    if (!well_formed || malformed) {
        if (last_errno_ == 0) last_errno_ = EPROTO;
        return -1;
    }
    if (last_errno_ != 0) return -1;
    return complete_ ? 1 : 0;
}

#ifdef KEEN_PBR3_TESTING
int ConntrackDump::consume_datagram_for_test(ByteView datagram) {
    return process_datagram(datagram);
}
#endif

ConntrackDeleteBatch::ConntrackDeleteBatch(const std::vector<ConntrackEntry>& entries,
                                           const ConntrackOptions& options)
    : socket_(options.rcvbuf_bytes, true), tx_(entries.size() * 128 + 64),
      rx_(kRxBufSize) {
    if (entries.size() > options.max_delete_batch) {
        throw NlSocketError("conntrack delete batch exceeds configured bound");
    }
    std::vector<const ConntrackEntry*> candidates;
    candidates.reserve(entries.size());
    for (const ConntrackEntry& entry : entries) {
        if (entry.deletion_candidate()) {
            candidates.push_back(&entry);
        } else {
            ++skipped_;
        }
    }
    if (candidates.empty()) {
        complete_ = true;
        return;
    }
    first_seq_ = socket_.next_seq();
    acked_.assign(candidates.size(), false);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const uint32_t seq = i == 0 ? first_seq_ : socket_.next_seq();
        build_conntrack_delete(tx_, seq, *candidates[i]);
    }
    int error = 0;
    if (!socket_.send(tx_.data(), tx_.size(), error)) {
        throw NlSocketError(std::string("conntrack delete send: ") + std::strerror(error));
    }
    pending_ = candidates.size();
}

int ConntrackDeleteBatch::receive(int timeout_ms) {
    if (complete_) return failures_ == 0 ? 1 : -1;
    const int ready = poll_readable(socket_.fd(), timeout_ms);
    if (ready < 0) {
        last_errno_ = errno;
        return -1;
    }
    if (ready == 0) return 0;
    int recv_error = 0;
    const ssize_t length = socket_.recv(rx_, recv_error);
    if (length < 0) {
        last_errno_ = recv_error;
        return -1;
    }
    bool malformed = false;
    const bool well_formed = for_each_msg(
        ByteView(rx_.data(), static_cast<std::size_t>(length)),
        [&](const MsgView& message) {
            if (message.type != NLMSG_ERROR) return true;
            const uint32_t index = message.seq - first_seq_;
            if (index >= acked_.size()) return true;
            int error = 0;
            uint32_t original_seq = 0;
            if (!parse_error(message, error, original_seq)) {
                if (!acked_[index]) {
                    acked_[index] = true;
                    --pending_;
                    ++failures_;
                }
                last_errno_ = EPROTO;
                malformed = true;
                return true;
            }
            if (acked_[index]) return true;
            acked_[index] = true;
            --pending_;
            if (error == 0) {
                ++succeeded_;
            } else {
                ++failures_;
                if (last_errno_ == 0) last_errno_ = error;
            }
            return true;
        });
    if (!well_formed || malformed) {
        if (last_errno_ == 0) last_errno_ = EPROTO;
        return -1;
    }
    if (pending_ != 0) return 0;
    complete_ = true;
    return failures_ == 0 ? 1 : -1;
}

} // namespace keen_pbr3::nfnl
