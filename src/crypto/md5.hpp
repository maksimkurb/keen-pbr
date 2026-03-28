#pragma once

// Public-domain MD5 implementation (single header, no dependencies).
// API: keen_pbr3::crypto::md5_hex(std::string_view) -> std::string (32 hex chars)

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace keen_pbr3::crypto {

namespace detail {

struct MD5State {
    uint32_t a = 0x67452301u;
    uint32_t b = 0xefcdab89u;
    uint32_t c = 0x98badcfeu;
    uint32_t d = 0x10325476u;
    uint64_t count = 0; // bits processed so far
    uint8_t buf[64] = {};
    uint32_t buf_len = 0;

    static constexpr uint32_t S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
    };

    static constexpr uint32_t K[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
        0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
        0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
        0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
        0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
        0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
    };

    static uint32_t rotl(uint32_t x, uint32_t n) {
        return (x << n) | (x >> (32u - n));
    }

    void process_block(const uint8_t* block) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = uint32_t(block[i*4])
                 | uint32_t(block[i*4+1]) << 8
                 | uint32_t(block[i*4+2]) << 16
                 | uint32_t(block[i*4+3]) << 24;
        }

        uint32_t aa = a, bb = b, cc = c, dd = d;
        for (int i = 0; i < 64; ++i) {
            uint32_t F, g;
            if      (i < 16) { F = (bb & cc) | (~bb & dd); g = uint32_t(i); }
            else if (i < 32) { F = (dd & bb) | (~dd & cc); g = (5u*i + 1u) % 16u; }
            else if (i < 48) { F = bb ^ cc ^ dd;            g = (3u*i + 5u) % 16u; }
            else             { F = cc ^ (bb | ~dd);          g = (7u*i)      % 16u; }
            F += aa + K[i] + M[g];
            aa = dd;
            dd = cc;
            cc = bb;
            bb += rotl(F, S[i]);
        }
        a += aa; b += bb; c += cc; d += dd;
    }

    void update(const uint8_t* data, size_t len) {
        count += uint64_t(len) * 8u;
        while (len > 0) {
            size_t space = 64u - buf_len;
            size_t take  = (len < space) ? len : space;
            std::memcpy(buf + buf_len, data, take);
            buf_len += uint32_t(take);
            data    += take;
            len     -= take;
            if (buf_len == 64) {
                process_block(buf);
                buf_len = 0;
            }
        }
    }

    std::array<uint8_t, 16> finalize() {
        // Padding
        uint8_t pad[64] = {};
        pad[0] = 0x80u;
        size_t pad_len = (buf_len < 56) ? (56u - buf_len) : (120u - buf_len);
        const uint64_t message_bits = count;
        update(pad, pad_len);

        // Length
        uint8_t len_bytes[8];
        for (int i = 0; i < 8; ++i)
            len_bytes[i] = uint8_t(message_bits >> (i * 8));
        update(len_bytes, 8);

        std::array<uint8_t, 16> result;
        for (int i = 0; i < 4; ++i) {
            result[i]    = uint8_t(a >> (i*8));
            result[i+4]  = uint8_t(b >> (i*8));
            result[i+8]  = uint8_t(c >> (i*8));
            result[i+12] = uint8_t(d >> (i*8));
        }
        return result;
    }

    std::array<uint8_t, 16> digest() const {
        MD5State copy = *this;
        return copy.finalize();
    }
};

} // namespace detail

inline std::string digest_to_hex(const std::array<uint8_t, 16>& digest) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out(32, '\0');
    for (int i = 0; i < 16; ++i) {
        out[i*2]   = hex[digest[i] >> 4];
        out[i*2+1] = hex[digest[i] & 0xf];
    }
    return out;
}

inline std::string md5_hex(std::string_view data) {
    detail::MD5State state;
    state.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return digest_to_hex(state.digest());
}

} // namespace keen_pbr3::crypto
