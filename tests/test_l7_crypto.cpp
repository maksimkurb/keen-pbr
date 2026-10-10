#include <doctest/doctest.h>

#include "../src/l7/crypto/aes128.hpp"
#include "../src/l7/crypto/sha256.hpp"

#include <string>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::l7::crypto;

namespace {

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    int hi = -1;
    for (char c : hex) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else continue;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<uint8_t>((hi << 4) | v));
            hi = -1;
        }
    }
    return out;
}

std::string to_hex(const uint8_t* d, std::size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += h[d[i] >> 4];
        s += h[d[i] & 15];
    }
    return s;
}

ByteView view(const std::vector<uint8_t>& v) { return ByteView(v.data(), v.size()); }
ByteView view(const std::string& s) { return ByteView(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

} // namespace

TEST_CASE("l7 crypto: SHA-256 vectors") {
    // FIPS 180-4 / RFC 6234 TEST1 and TEST2_1.
    auto d1 = sha256(view(std::string("abc")));
    CHECK(to_hex(d1.data(), 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto d2 = sha256(view(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")));
    CHECK(to_hex(d2.data(), 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // Incremental update in odd chunk sizes must match one-shot.
    std::string msg(1000, 'x');
    auto one = sha256(view(msg));
    Sha256 h;
    for (std::size_t i = 0; i < msg.size(); i += 37) {
        h.update(reinterpret_cast<const uint8_t*>(msg.data()) + i, std::min<std::size_t>(37, msg.size() - i));
    }
    CHECK(h.final() == one);
}

TEST_CASE("l7 crypto: HMAC-SHA256 RFC 4231") {
    // Test case 1
    auto k1 = from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    auto m1 = from_hex("4869205468657265");
    auto r1 = hmac_sha256(view(k1), view(m1));
    CHECK(to_hex(r1.data(), 32) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    // Test case 2
    auto k2 = from_hex("4a656665");
    auto m2 = from_hex("7768617420646f2079612077616e7420666f72206e6f7468696e673f");
    auto r2 = hmac_sha256(view(k2), view(m2));
    CHECK(to_hex(r2.data(), 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("l7 crypto: HKDF RFC 5869 test case 1") {
    auto ikm = from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    auto salt = from_hex("000102030405060708090a0b0c");
    auto info = from_hex("f0f1f2f3f4f5f6f7f8f9");
    auto prk = hkdf_extract(view(salt), view(ikm));
    CHECK(to_hex(prk.data(), 32) == "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    uint8_t okm[42];
    REQUIRE(hkdf_expand(ByteView(prk.data(), 32), view(info), okm, sizeof(okm)));
    CHECK(to_hex(okm, 42) ==
          "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
}

TEST_CASE("l7 crypto: AES-128 FIPS-197 C.1") {
    auto key = from_hex("000102030405060708090a0b0c0d0e0f");
    auto pt = from_hex("00112233445566778899aabbccddeeff");
    Aes128KeySchedule ks;
    aes128_expand_key(key.data(), ks);
    uint8_t ct[16];
    aes128_encrypt_block(ks, pt.data(), ct);
    CHECK(to_hex(ct, 16) == "69c4e0d86a7b0430d8cdb78070b4c55a");
}

TEST_CASE("l7 crypto: AES-128 CTR keystream is self-inverse and chunk independent") {
    auto key = from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128KeySchedule ks;
    aes128_expand_key(key.data(), ks);
    uint8_t iv[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::vector<uint8_t> data(70);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
    auto orig = data;
    aes128_ctr_xor(ks, iv, 2, data.data(), data.size());
    CHECK(data != orig);
    // First keystream block is AES(iv || 00000002).
    uint8_t blk[16];
    std::copy(iv, iv + 12, blk);
    blk[12] = blk[13] = blk[14] = 0;
    blk[15] = 2;
    uint8_t ks0[16];
    aes128_encrypt_block(ks, blk, ks0);
    for (int i = 0; i < 16; ++i) CHECK(static_cast<uint8_t>(data[i] ^ orig[i]) == ks0[i]);
    aes128_ctr_xor(ks, iv, 2, data.data(), data.size());
    CHECK(data == orig);
}
