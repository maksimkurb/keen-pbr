#include "dns_wire.hpp"

#include "../util/hostname_validation.hpp"

#include <algorithm>
#include <netinet/in.h>

namespace keen_pbr3::dns_wire {

namespace {

constexpr std::size_t kMaxNameWire = 255;
constexpr std::size_t kMaxLabel = 63;
constexpr int kMaxJumps = 32;

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t rd32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void wr16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

char lower(uint8_t c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : static_cast<char>(c);
}

// Decodes a (possibly compressed) name starting at `pos`. On success `next` is the offset just
// after the name at its original (non-jumped) position.
bool read_name(const uint8_t* msg, std::size_t size, std::size_t pos, std::string& out,
               std::size_t& next) {
    out.clear();
    std::size_t wire_len = 1; // terminating root byte
    int jumps = 0;
    bool jumped = false;
    std::size_t next_pos = 0;
    while (true) {
        if (pos >= size) {
            return false;
        }
        const uint8_t len = msg[pos];
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= size) {
                return false;
            }
            const std::size_t target = static_cast<std::size_t>(((len & 0x3F) << 8) | msg[pos + 1]);
            if (!jumped) {
                next_pos = pos + 2;
                jumped = true;
            }
            if (++jumps > kMaxJumps || target >= size) {
                return false;
            }
            pos = target;
            continue;
        }
        if ((len & 0xC0) != 0) {
            return false;
        }
        if (len == 0) {
            next = jumped ? next_pos : pos + 1;
            return true;
        }
        if (len > kMaxLabel || pos + 1 + len > size) {
            return false;
        }
        wire_len += 1u + len;
        if (wire_len > kMaxNameWire) {
            return false;
        }
        if (!out.empty()) {
            out.push_back('.');
        }
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(lower(msg[pos + 1 + i]));
        }
        pos += 1u + len;
    }
}

void add_unique(std::vector<std::string>& v, const std::string& s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) {
        v.push_back(s);
    }
}

uint32_t sum_words(const uint8_t* data, std::size_t len, uint32_t sum) {
    std::size_t i = 0;
    for (; i + 1 < len; i += 2) {
        sum += rd16(data + i);
    }
    if (i < len) {
        sum += static_cast<uint32_t>(data[i]) << 8;
    }
    return sum;
}

uint16_t fold_not(uint32_t sum) {
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum & 0xFFFF);
}

} // namespace

uint16_t inet_checksum(const uint8_t* data, std::size_t len, uint32_t initial_sum) {
    return fold_not(sum_words(data, len, initial_sum));
}

std::optional<PacketLayout> parse_packet_layout(ByteView pkt) {
    const uint8_t* d = pkt.data();
    const std::size_t size = pkt.size();
    if (size < 1) {
        return std::nullopt;
    }
    PacketLayout l;
    std::size_t end = 0;
    uint8_t proto = 0;
    const uint8_t version = d[0] >> 4;
    if (version == 4) {
        if (size < 20) {
            return std::nullopt;
        }
        const std::size_t ihl = static_cast<std::size_t>(d[0] & 0x0F) * 4;
        if (ihl < 20 || ihl > size) {
            return std::nullopt;
        }
        const uint16_t frag = rd16(d + 6);
        if ((frag & 0x2000) != 0 || (frag & 0x1FFF) != 0) {
            return std::nullopt;
        }
        const std::size_t total = rd16(d + 2);
        if (total < ihl || total > size) {
            return std::nullopt;
        }
        l.ip_version = 4;
        l.l3_header_len = ihl;
        end = total;
        proto = d[9];
    } else if (version == 6) {
        if (size < 40) {
            return std::nullopt;
        }
        end = 40 + static_cast<std::size_t>(rd16(d + 4));
        if (end > size) {
            return std::nullopt;
        }
        proto = d[6];
        std::size_t off = 40;
        while (proto == 0 || proto == 43 || proto == 60) {
            if (off + 2 > end) {
                return std::nullopt;
            }
            const std::size_t elen = (static_cast<std::size_t>(d[off + 1]) + 1) * 8;
            if (off + elen > end) {
                return std::nullopt;
            }
            proto = d[off];
            off += elen;
        }
        l.ip_version = 6;
        l.l3_header_len = off;
    } else {
        return std::nullopt;
    }

    const std::size_t l4 = l.l3_header_len;
    if (proto == IPPROTO_UDP) {
        if (l4 + 8 > end) {
            return std::nullopt;
        }
        l.l4_header_len = 8;
    } else if (proto == IPPROTO_TCP) {
        if (l4 + 20 > end) {
            return std::nullopt;
        }
        const std::size_t doff = static_cast<std::size_t>(d[l4 + 12] >> 4) * 4;
        if (doff < 20 || l4 + doff > end) {
            return std::nullopt;
        }
        l.l4_header_len = doff;
    } else {
        return std::nullopt;
    }
    l.l4_proto = proto;
    l.payload_offset = l.l3_header_len + l.l4_header_len;
    l.payload_len = end - l.payload_offset;
    return l;
}

void ParsedResponse::clear() {
    id = 0;
    flags = 0;
    rcode = 0;
    truncated = false;
    qtype = 0;
    qclass = 0;
    qname.clear();
    question_end = 0;
    cname_chain.clear();
    addresses.clear();
}

bool parse_response(ByteView dns_message, ParsedResponse& out) {
    out.clear();
    const uint8_t* m = dns_message.data();
    const std::size_t size = dns_message.size();
    if (size < 12) {
        return false;
    }
    out.id = rd16(m);
    out.flags = rd16(m + 2);
    if ((out.flags & 0x8000) == 0) {
        return false;
    }
    out.rcode = static_cast<uint8_t>(out.flags & 0x0F);
    out.truncated = (out.flags & 0x0200) != 0;
    if (rd16(m + 4) != 1) {
        return false;
    }
    const uint16_t ancount = rd16(m + 6);

    std::size_t pos = 12;
    if (!read_name(m, size, pos, out.qname, pos)) {
        return false;
    }
    if (!is_valid_dns_name(out.qname)) {
        return false;
    }
    if (pos + 4 > size) {
        return false;
    }
    out.qtype = rd16(m + pos);
    out.qclass = rd16(m + pos + 2);
    pos += 4;
    out.question_end = pos;

    std::string owner;
    std::string target;
    for (uint16_t i = 0; i < ancount; ++i) {
        if (!read_name(m, size, pos, owner, pos)) {
            return false;
        }
        if (pos + 10 > size) {
            return false;
        }
        const uint16_t type = rd16(m + pos);
        const uint16_t cls = rd16(m + pos + 2);
        const uint32_t ttl = rd32(m + pos + 4);
        const std::size_t rdlen = rd16(m + pos + 8);
        pos += 10;
        if (pos + rdlen > size) {
            return false;
        }
        if (type == 5) {
            std::size_t ignored = 0;
            if (!read_name(m, size, pos, target, ignored)) {
                return false;
            }
            // A name with non-hostname bytes is never learnable.
            if (is_valid_dns_name(owner)) {
                add_unique(out.cname_chain, owner);
            }
            if (is_valid_dns_name(target)) {
                add_unique(out.cname_chain, target);
            }
        } else if ((type == 1 || type == 28) && cls == 1) {
            const std::size_t want = (type == 1) ? 4 : 16;
            if (rdlen != want) {
                return false;
            }
            AddressRecord rec;
            rec.family = (type == 1) ? 4 : 6;
            std::copy(m + pos, m + pos + want, rec.addr.begin());
            rec.ttl = ttl;
            out.addresses.push_back(rec);
        }
        pos += rdlen;
    }
    return true;
}

std::optional<ByteView> tcp_single_message(ByteView tcp_payload) {
    if (tcp_payload.size() < 2) {
        return std::nullopt;
    }
    const std::size_t len = rd16(tcp_payload.data());
    if (len + 2 > tcp_payload.size()) {
        return std::nullopt;
    }
    return ByteView(tcp_payload.data() + 2, len);
}

bool is_marker_name(std::string_view qname, std::string_view marker) {
    if (marker.empty() || qname.size() < marker.size()) {
        return false;
    }
    const std::size_t off = qname.size() - marker.size();
    for (std::size_t i = 0; i < marker.size(); ++i) {
        if (lower(static_cast<uint8_t>(qname[off + i])) != lower(static_cast<uint8_t>(marker[i]))) {
            return false;
        }
    }
    return off == 0 || qname[off - 1] == '.';
}

std::optional<std::vector<uint8_t>> build_marker_packet(ByteView original,
                                                        const PacketLayout& layout,
                                                        const ParsedResponse& resp,
                                                        const std::array<uint8_t, 4>& answer_ipv4) {
    if (layout.l4_proto != IPPROTO_UDP || layout.l4_header_len != 8) {
        return std::nullopt;
    }
    if (layout.ip_version != 4 && layout.ip_version != 6) {
        return std::nullopt;
    }
    if (layout.ip_version == 6 && layout.l3_header_len != 40) {
        return std::nullopt;
    }
    const std::size_t l3 = layout.l3_header_len;
    if (l3 + 8 > original.size()) {
        return std::nullopt;
    }
    const bool answer = (resp.qtype == 1);

    std::vector<uint8_t> pkt(original.data(), original.data() + l3 + 8);
    pkt.reserve(l3 + 8 + 12 + resp.qname.size() + 2 + 4 + 16);

    const std::size_t dns_off = pkt.size();
    pkt.resize(dns_off + 12);
    uint8_t* h = pkt.data() + dns_off;
    wr16(h, resp.id);
    // QR | AA | RD (copied) | RA
    wr16(h + 2, static_cast<uint16_t>(0x8000 | 0x0400 | (resp.flags & 0x0100) | 0x0080));
    wr16(h + 4, 1);
    wr16(h + 6, answer ? 1 : 0);
    wr16(h + 8, 0);
    wr16(h + 10, 0);
    const std::size_t orig_dns = layout.payload_offset;
    if (resp.question_end < 12 || orig_dns + resp.question_end > original.size()) {
        return std::nullopt;
    }
    pkt.insert(pkt.end(), original.data() + orig_dns + 12, original.data() + orig_dns + resp.question_end);
    if (answer) {
        const uint8_t rr[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
                              answer_ipv4[0], answer_ipv4[1], answer_ipv4[2], answer_ipv4[3]};
        pkt.insert(pkt.end(), rr, rr + sizeof(rr));
    }

    if (pkt.size() > 0xFFFF) {
        return std::nullopt;
    }
    const std::size_t udp_len = pkt.size() - l3;
    uint8_t* udp = pkt.data() + l3;
    wr16(udp + 4, static_cast<uint16_t>(udp_len));
    udp[6] = 0;
    udp[7] = 0;

    uint32_t sum = 0;
    if (layout.ip_version == 4) {
        wr16(pkt.data() + 2, static_cast<uint16_t>(pkt.size()));
        pkt[10] = 0;
        pkt[11] = 0;
        wr16(pkt.data() + 10, inet_checksum(pkt.data(), l3));
        sum = sum_words(pkt.data() + 12, 8, 0);
    } else {
        wr16(pkt.data() + 4, static_cast<uint16_t>(udp_len));
        pkt[6] = IPPROTO_UDP;
        sum = sum_words(pkt.data() + 8, 32, 0);
    }
    sum += static_cast<uint32_t>(udp_len) + IPPROTO_UDP;
    uint16_t csum = inet_checksum(udp, udp_len, sum);
    if (csum == 0) {
        csum = 0xFFFF;
    }
    wr16(udp + 6, csum);
    return pkt;
}

} // namespace keen_pbr3::dns_wire
