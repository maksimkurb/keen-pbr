#include "quic_initial.hpp"

#include <algorithm>
#include <cstring>

#include "crypto/aes128.hpp"
#include "crypto/sha256.hpp"

namespace keen_pbr3::l7 {

namespace {

const uint8_t kSaltV1[20] = {0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
                             0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a};
const uint8_t kSaltV2[20] = {0x0d, 0xed, 0xe3, 0xde, 0xf7, 0x00, 0xa6, 0xdb, 0x81, 0x93,
                             0x81, 0xbe, 0x6e, 0x26, 0x9d, 0xcb, 0xf9, 0xbd, 0x2e, 0xd9};

constexpr std::size_t kTagLen = 16;
constexpr std::size_t kMaxDcidLen = 20;

bool read_varint(ByteView data, std::size_t& pos, uint64_t& out) {
    if (pos >= data.size()) {
        return false;
    }
    const uint8_t first = data.data()[pos];
    const std::size_t len = static_cast<std::size_t>(1) << (first >> 6);
    if (data.size() - pos < len) {
        return false;
    }
    uint64_t v = first & 0x3f;
    for (std::size_t i = 1; i < len; ++i) {
        v = (v << 8) | data.data()[pos + i];
    }
    pos += len;
    out = v;
    return true;
}

bool supported_version(uint32_t v, QuicVersion& out) {
    if (v == static_cast<uint32_t>(QuicVersion::V1)) {
        out = QuicVersion::V1;
        return true;
    }
    if (v == static_cast<uint32_t>(QuicVersion::V2)) {
        out = QuicVersion::V2;
        return true;
    }
    return false;
}

// Parses frames in the decrypted payload. Stops silently at the first unknown/malformed frame.
void parse_frames(ByteView payload, const std::function<void(uint64_t, ByteView)>& on_crypto) {
    std::size_t pos = 0;
    while (pos < payload.size()) {
        uint64_t type = 0;
        if (!read_varint(payload, pos, type)) {
            return;
        }
        if (type == 0x00 || type == 0x01) {
            continue;  // PADDING, PING
        }
        if (type == 0x02 || type == 0x03) {
            uint64_t v = 0;
            uint64_t range_count = 0;
            if (!read_varint(payload, pos, v) ||            // largest acknowledged
                !read_varint(payload, pos, v) ||            // ack delay
                !read_varint(payload, pos, range_count) ||  // range count
                !read_varint(payload, pos, v)) {            // first ack range
                return;
            }
            for (uint64_t i = 0; i < range_count; ++i) {
                if (!read_varint(payload, pos, v) || !read_varint(payload, pos, v)) {
                    return;
                }
            }
            if (type == 0x03) {
                for (int i = 0; i < 3; ++i) {
                    if (!read_varint(payload, pos, v)) {
                        return;
                    }
                }
            }
            continue;
        }
        if (type == 0x06) {
            uint64_t offset = 0;
            uint64_t length = 0;
            if (!read_varint(payload, pos, offset) || !read_varint(payload, pos, length)) {
                return;
            }
            if (length > payload.size() - pos) {
                return;
            }
            on_crypto(offset, ByteView(payload.data() + pos, static_cast<std::size_t>(length)));
            pos += static_cast<std::size_t>(length);
            continue;
        }
        return;  // CONNECTION_CLOSE or anything unexpected in an Initial
    }
}

} // namespace

bool derive_client_initial_keys(QuicVersion v, ByteView dcid, QuicInitialKeys& out) {
    using namespace crypto;
    const bool v2 = (v == QuicVersion::V2);
    if (!v2 && v != QuicVersion::V1) {
        return false;
    }
    const Sha256Digest initial_secret =
        hkdf_extract(ByteView(v2 ? kSaltV2 : kSaltV1, 20), dcid);
    uint8_t client_secret[32];
    if (!hkdf_expand_label(ByteView(initial_secret.data(), initial_secret.size()), "client in", client_secret, 32)) {
        return false;
    }
    const ByteView secret(client_secret, 32);
    return hkdf_expand_label(secret, v2 ? "quicv2 key" : "quic key", out.key.data(), out.key.size()) &&
           hkdf_expand_label(secret, v2 ? "quicv2 iv" : "quic iv", out.iv.data(), out.iv.size()) &&
           hkdf_expand_label(secret, v2 ? "quicv2 hp" : "quic hp", out.hp.data(), out.hp.size());
}

bool decrypt_initial_datagram(ByteView udp_payload, QuicInitialPacketInfo& info,
                              const std::function<void(uint64_t offset, ByteView data)>& on_crypto,
                              std::vector<uint8_t>& scratch) {
    const uint8_t* const base = udp_payload.data();
    const std::size_t total = udp_payload.size();
    std::size_t start = 0;
    bool first = true;

    uint8_t cached_dcid[kMaxDcidLen];
    std::size_t cached_dcid_len = 0;
    QuicVersion cached_version = QuicVersion::V1;
    bool have_keys = false;
    QuicInitialKeys keys{};
    crypto::Aes128KeySchedule hp_ks;
    crypto::Aes128KeySchedule key_ks;

    while (start < total) {
        const ByteView pkt(base + start, total - start);
        // Long header: flags(1) version(4) dcid_len(1) dcid scid_len(1) scid token_len(v) token length(v) pn...
        if (pkt.size() < 7 || (pkt.data()[0] & 0x80) == 0) {
            return !first;
        }
        const uint32_t ver = (static_cast<uint32_t>(pkt.data()[1]) << 24) | (static_cast<uint32_t>(pkt.data()[2]) << 16) |
                             (static_cast<uint32_t>(pkt.data()[3]) << 8) | pkt.data()[4];
        QuicVersion version{};
        if (!supported_version(ver, version)) {
            return !first;
        }
        const unsigned type_bits = (pkt.data()[0] >> 4) & 0x03;
        const unsigned initial_type = (version == QuicVersion::V2) ? 1u : 0u;
        if (type_bits != initial_type) {
            return !first;
        }

        std::size_t pos = 5;
        const std::size_t dcid_len = pkt.data()[pos++];
        if (dcid_len > kMaxDcidLen || pkt.size() - pos < dcid_len + 1) {
            return !first;
        }
        const uint8_t* dcid = pkt.data() + pos;
        pos += dcid_len;
        const std::size_t scid_len = pkt.data()[pos++];
        if (pkt.size() - pos < scid_len) {
            return !first;
        }
        pos += scid_len;
        uint64_t token_len = 0;
        if (!read_varint(pkt, pos, token_len) || token_len > pkt.size() - pos) {
            return !first;
        }
        pos += static_cast<std::size_t>(token_len);
        uint64_t length = 0;
        if (!read_varint(pkt, pos, length) || length > pkt.size() - pos) {
            return !first;
        }
        const std::size_t pn_offset = pos;
        const std::size_t pkt_end = pn_offset + static_cast<std::size_t>(length);
        // Need 4 bytes of (max) packet number, then a 16 byte header-protection sample, and the tag.
        if (length < 4 + 16 || length < 4 + kTagLen) {
            return !first;
        }

        if (first) {
            info.version = version;
            info.dcid_len = static_cast<uint8_t>(dcid_len);
            info.dcid.fill(0);
            std::memcpy(info.dcid.data(), dcid, dcid_len);
        }

        if (!have_keys || cached_version != version || cached_dcid_len != dcid_len ||
            std::memcmp(cached_dcid, dcid, dcid_len) != 0) {
            if (!derive_client_initial_keys(version, ByteView(dcid, dcid_len), keys)) {
                return !first;
            }
            crypto::aes128_expand_key(keys.hp.data(), hp_ks);
            crypto::aes128_expand_key(keys.key.data(), key_ks);
            std::memcpy(cached_dcid, dcid, dcid_len);
            cached_dcid_len = dcid_len;
            cached_version = version;
            have_keys = true;
        }

        // Header protection removal (RFC 9001 section 5.4).
        uint8_t mask[16];
        crypto::aes128_encrypt_block(hp_ks, pkt.data() + pn_offset + 4, mask);
        const uint8_t flags = static_cast<uint8_t>(pkt.data()[0] ^ (mask[0] & 0x0f));
        const std::size_t pn_len = static_cast<std::size_t>(flags & 0x03) + 1;
        if (length < pn_len + kTagLen) {
            return !first;
        }
        uint8_t nonce[12];
        std::memcpy(nonce, keys.iv.data(), 12);
        for (std::size_t i = 0; i < pn_len; ++i) {
            const uint8_t pn_byte = static_cast<uint8_t>(pkt.data()[pn_offset + i] ^ mask[1 + i]);
            // Packet number is left-padded big-endian into the low bytes of the nonce.
            nonce[12 - pn_len + i] ^= pn_byte;
        }

        const std::size_t payload_start = pn_offset + pn_len;
        const std::size_t cipher_len = pkt_end - payload_start - kTagLen;
        scratch.assign(pkt.data() + payload_start, pkt.data() + payload_start + cipher_len);
        // Tag is intentionally not verified (see aes128.hpp).
        crypto::aes128_ctr_xor(key_ks, nonce, 2, scratch.data(), scratch.size());
        parse_frames(ByteView(scratch.data(), scratch.size()), on_crypto);

        first = false;
        start += (pkt_end - 0);  // pkt_end is relative to this packet start
    }
    return !first;
}

QuicCryptoAssembler::QuicCryptoAssembler(std::size_t max_conns, std::size_t max_bytes,
                                         std::chrono::milliseconds ttl)
    : max_conns_(max_conns == 0 ? 1 : max_conns)
    , max_bytes_(max_bytes)
    , ttl_(ttl) {}

std::size_t QuicCryptoAssembler::size() const {
    return lru_.size();
}

void QuicCryptoAssembler::expire_(std::chrono::steady_clock::time_point now) {
    while (!lru_.empty() && now - lru_.back().last_seen > ttl_) {
        index_.erase(lru_.back().key);
        lru_.pop_back();
    }
}

void QuicCryptoAssembler::add_fragment_(Conn& c, uint64_t offset, ByteView data) {
    if (data.size() == 0 || offset >= max_bytes_) {
        return;
    }
    uint64_t end = offset + data.size();
    if (end > max_bytes_) {
        end = max_bytes_;
    }
    if (c.buf.size() < end) {
        c.buf.resize(static_cast<std::size_t>(end), 0);
    }
    std::memcpy(c.buf.data() + offset, data.data(), static_cast<std::size_t>(end - offset));

    // Insert [offset, end) into the sorted merged range list.
    uint64_t ns = offset;
    uint64_t ne = end;
    std::vector<std::pair<uint64_t, uint64_t>> merged;
    merged.reserve(c.ranges.size() + 1);
    bool placed = false;
    for (const auto& r : c.ranges) {
        if (r.second < ns) {
            merged.push_back(r);
        } else if (r.first > ne) {
            if (!placed) {
                merged.emplace_back(ns, ne);
                placed = true;
            }
            merged.push_back(r);
        } else {
            ns = std::min(ns, r.first);
            ne = std::max(ne, r.second);
        }
    }
    if (!placed) {
        merged.emplace_back(ns, ne);
    }
    c.ranges.swap(merged);
}

ByteView QuicCryptoAssembler::feed(ByteView udp_payload, std::chrono::steady_clock::time_point now) {
    last_key_.clear();
    expire_(now);

    QuicInitialPacketInfo info;
    Conn* conn = nullptr;
    auto on_crypto = [&](uint64_t offset, ByteView data) {
        if (conn == nullptr) {
            std::string key("k");  // non-empty even for a zero-length DCID
            key.append(reinterpret_cast<const char*>(info.dcid.data()), info.dcid_len);
            auto it = index_.find(key);
            if (it != index_.end()) {
                lru_.splice(lru_.begin(), lru_, it->second);
            } else {
                while (lru_.size() >= max_conns_) {
                    index_.erase(lru_.back().key);
                    lru_.pop_back();
                }
                lru_.emplace_front();
                lru_.front().key = key;
                index_[key] = lru_.begin();
            }
            conn = &lru_.front();
            conn->last_seen = now;
            last_key_ = key;
        }
        add_fragment_(*conn, offset, data);
    };

    if (!decrypt_initial_datagram(udp_payload, info, on_crypto, scratch_) || conn == nullptr) {
        return ByteView();
    }
    if (conn->ranges.empty() || conn->ranges.front().first != 0) {
        return ByteView();
    }
    return ByteView(conn->buf.data(), static_cast<std::size_t>(conn->ranges.front().second));
}

void QuicCryptoAssembler::erase_last() {
    if (last_key_.empty()) {
        return;
    }
    auto it = index_.find(last_key_);
    if (it != index_.end()) {
        lru_.erase(it->second);
        index_.erase(it);
    }
    last_key_.clear();
}

} // namespace keen_pbr3::l7
