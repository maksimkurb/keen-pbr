#include "sha256.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace keen_pbr3::l7::crypto {

namespace {

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
    , buffer_{} {}

void Sha256::process_block_(const uint8_t* block) {
    uint32_t w[64];
    for (std::ptrdiff_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const uint8_t* data, std::size_t len) {
    total_len_ += len;
    while (len > 0) {
        const std::size_t take = std::min<std::size_t>(len, 64 - buffer_len_);
        std::memcpy(buffer_ + buffer_len_, data, take);
        buffer_len_ += take;
        data += take;
        len -= take;
        if (buffer_len_ == 64) {
            process_block_(buffer_);
            buffer_len_ = 0;
        }
    }
}

Sha256Digest Sha256::final() {
    const uint64_t bit_len = total_len_ * 8;
    const uint8_t pad_start = 0x80;
    update(&pad_start, 1);
    const uint8_t zero = 0;
    while (buffer_len_ != 56) {
        update(&zero, 1);
    }
    uint8_t len_bytes[8];
    for (int i = 0; i < 8; ++i) {
        len_bytes[i] = static_cast<uint8_t>(bit_len >> (56 - 8 * i));
    }
    update(len_bytes, 8);

    Sha256Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
    }
    return out;
}

Sha256Digest sha256(ByteView data) {
    Sha256 h;
    h.update(data.data(), data.size());
    return h.final();
}

Sha256Digest hmac_sha256(ByteView key, ByteView message) {
    uint8_t k[64] = {};
    if (key.size() > 64) {
        const Sha256Digest kd = sha256(key);
        std::memcpy(k, kd.data(), kd.size());
    } else if (key.size() > 0) {
        std::memcpy(k, key.data(), key.size());
    }
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(message.data(), message.size());
    const Sha256Digest inner_digest = inner.final();
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(inner_digest.data(), inner_digest.size());
    return outer.final();
}

Sha256Digest hkdf_extract(ByteView salt, ByteView ikm) {
    // RFC 5869: an absent salt means HashLen zero bytes, which HMAC's zero padding yields anyway.
    return hmac_sha256(salt, ikm);
}

bool hkdf_expand(ByteView prk, ByteView info, uint8_t* out, std::size_t out_len) {
    if (out_len > std::size_t{255} * 32U) {
        return false;
    }
    Sha256Digest t{};
    std::size_t t_len = 0;
    std::size_t produced = 0;
    uint8_t counter = 1;
    std::vector<uint8_t> msg;
    while (produced < out_len) {
        msg.assign(t.begin(), t.begin() + t_len);
        msg.insert(msg.end(), info.begin(), info.end());
        msg.push_back(counter++);
        t = hmac_sha256(prk, ByteView(msg.data(), msg.size()));
        t_len = t.size();
        const std::size_t take = std::min<std::size_t>(t_len, out_len - produced);
        std::memcpy(out + produced, t.data(), take);
        produced += take;
    }
    return true;
}

bool hkdf_expand_label(ByteView secret, const char* label, uint8_t* out, std::size_t out_len) {
    static const char kPrefix[] = "tls13 ";
    const std::size_t prefix_len = sizeof(kPrefix) - 1;
    const std::size_t label_len = std::strlen(label);
    if (prefix_len + label_len > 255 || out_len > 0xffff) {
        return false;
    }
    std::vector<uint8_t> info;
    info.reserve(4 + prefix_len + label_len);
    info.push_back(static_cast<uint8_t>(out_len >> 8));
    info.push_back(static_cast<uint8_t>(out_len));
    info.push_back(static_cast<uint8_t>(prefix_len + label_len));
    info.insert(info.end(), kPrefix, kPrefix + prefix_len);
    info.insert(info.end(), label, label + label_len);
    info.push_back(0);  // empty context
    return hkdf_expand(secret, ByteView(info.data(), info.size()), out, out_len);
}

} // namespace keen_pbr3::l7::crypto
