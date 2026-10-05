#include <doctest/doctest.h>

#include "../src/l7/crypto/aes128.hpp"
#include "../src/l7/quic_initial.hpp"

#include <string>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::l7;

namespace {

// Vectors copied verbatim from RFC 9001 Appendix A.2 (kV1*) and RFC 9369 Appendix A.2 (kV2*).
static const char* const kV1CryptoFrame =
    "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e868"
    "04fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578"
    "616d706c652e636f6dff01000100000a00080006001d00170018001000070005"
    "04616c706e000500050100000000003300260024001d00209370b2c9caa47fba"
    "baf4559fedba753de171fa71f50f1ce15d43e994ec74d748002b000302030400"
    "0d0010000e0403050306030203080408050806002d00020101001c0002400100"
    "3900320408ffffffffffffffff05048000ffff07048000ffff08011001048000"
    "75300901100f088394c8f03e51570806048000ffff"
;
static const char* const kV1ProtectedPacket =
    "c000000001088394c8f03e5157080000449e7b9aec34d1b1c98dd7689fb8ec11"
    "d242b123dc9bd8bab936b47d92ec356c0bab7df5976d27cd449f63300099f399"
    "1c260ec4c60d17b31f8429157bb35a1282a643a8d2262cad67500cadb8e7378c"
    "8eb7539ec4d4905fed1bee1fc8aafba17c750e2c7ace01e6005f80fcb7df6212"
    "30c83711b39343fa028cea7f7fb5ff89eac2308249a02252155e2347b63d58c5"
    "457afd84d05dfffdb20392844ae812154682e9cf012f9021a6f0be17ddd0c208"
    "4dce25ff9b06cde535d0f920a2db1bf362c23e596d11a4f5a6cf3948838a3aec"
    "4e15daf8500a6ef69ec4e3feb6b1d98e610ac8b7ec3faf6ad760b7bad1db4ba3"
    "485e8a94dc250ae3fdb41ed15fb6a8e5eba0fc3dd60bc8e30c5c4287e53805db"
    "059ae0648db2f64264ed5e39be2e20d82df566da8dd5998ccabdae053060ae6c"
    "7b4378e846d29f37ed7b4ea9ec5d82e7961b7f25a9323851f681d582363aa5f8"
    "9937f5a67258bf63ad6f1a0b1d96dbd4faddfcefc5266ba6611722395c906556"
    "be52afe3f565636ad1b17d508b73d8743eeb524be22b3dcbc2c7468d54119c74"
    "68449a13d8e3b95811a198f3491de3e7fe942b330407abf82a4ed7c1b311663a"
    "c69890f4157015853d91e923037c227a33cdd5ec281ca3f79c44546b9d90ca00"
    "f064c99e3dd97911d39fe9c5d0b23a229a234cb36186c4819e8b9c5927726632"
    "291d6a418211cc2962e20fe47feb3edf330f2c603a9d48c0fcb5699dbfe58964"
    "25c5bac4aee82e57a85aaf4e2513e4f05796b07ba2ee47d80506f8d2c25e50fd"
    "14de71e6c418559302f939b0e1abd576f279c4b2e0feb85c1f28ff18f58891ff"
    "ef132eef2fa09346aee33c28eb130ff28f5b766953334113211996d20011a198"
    "e3fc433f9f2541010ae17c1bf202580f6047472fb36857fe843b19f5984009dd"
    "c324044e847a4f4a0ab34f719595de37252d6235365e9b84392b061085349d73"
    "203a4a13e96f5432ec0fd4a1ee65accdd5e3904df54c1da510b0ff20dcc0c77f"
    "cb2c0e0eb605cb0504db87632cf3d8b4dae6e705769d1de354270123cb11450e"
    "fc60ac47683d7b8d0f811365565fd98c4c8eb936bcab8d069fc33bd801b03ade"
    "a2e1fbc5aa463d08ca19896d2bf59a071b851e6c239052172f296bfb5e724047"
    "90a2181014f3b94a4e97d117b438130368cc39dbb2d198065ae3986547926cd2"
    "162f40a29f0c3c8745c0f50fba3852e566d44575c29d39a03f0cda721984b6f4"
    "40591f355e12d439ff150aab7613499dbd49adabc8676eef023b15b65bfc5ca0"
    "6948109f23f350db82123535eb8a7433bdabcb909271a6ecbcb58b936a88cd4e"
    "8f2e6ff5800175f113253d8fa9ca8885c2f552e657dc603f252e1a8e308f76f0"
    "be79e2fb8f5d5fbbe2e30ecadd220723c8c0aea8078cdfcb3868263ff8f09400"
    "54da48781893a7e49ad5aff4af300cd804a6b6279ab3ff3afb64491c85194aab"
    "760d58a606654f9f4400e8b38591356fbf6425aca26dc85244259ff2b19c41b9"
    "f96f3ca9ec1dde434da7d2d392b905ddf3d1f9af93d1af5950bd493f5aa731b4"
    "056df31bd267b6b90a079831aaf579be0a39013137aac6d404f518cfd4684064"
    "7e78bfe706ca4cf5e9c5453e9f7cfd2b8b4c8d169a44e55c88d4a9a7f9474241"
    "e221af44860018ab0856972e194cd934"
;

static const char* const kV2CryptoFrame =
    "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e868"
    "04fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578"
    "616d706c652e636f6dff01000100000a00080006001d00170018001000070005"
    "04616c706e000500050100000000003300260024001d00209370b2c9caa47fba"
    "baf4559fedba753de171fa71f50f1ce15d43e994ec74d748002b000302030400"
    "0d0010000e0403050306030203080408050806002d00020101001c0002400100"
    "3900320408ffffffffffffffff05048000ffff07048000ffff08011001048000"
    "75300901100f088394c8f03e51570806048000ffff"
;
static const char* const kV2ProtectedPacket =
    "d76b3343cf088394c8f03e5157080000449ea0c95e82ffe67b6abcdb4298b485"
    "dd04de806071bf03dceebfa162e75d6c96058bdbfb127cdfcbf903388e99ad04"
    "9f9a3dd4425ae4d0992cfff18ecf0fdb5a842d09747052f17ac2053d21f57c5d"
    "250f2c4f0e0202b70785b7946e992e58a59ac52dea6774d4f03b55545243cf1a"
    "12834e3f249a78d395e0d18f4d766004f1a2674802a747eaa901c3f10cda5500"
    "cb9122faa9f1df66c392079a1b40f0de1c6054196a11cbea40afb6ef5253cd68"
    "18f6625efce3b6def6ba7e4b37a40f7732e093daa7d52190935b8da58976ff33"
    "12ae50b187c1433c0f028edcc4c2838b6a9bfc226ca4b4530e7a4ccee1bfa2a3"
    "d396ae5a3fb512384b2fdd851f784a65e03f2c4fbe11a53c7777c023462239dd"
    "6f7521a3f6c7d5dd3ec9b3f233773d4b46d23cc375eb198c63301c21801f6520"
    "bcfb7966fc49b393f0061d974a2706df8c4a9449f11d7f3d2dcbb90c6b877045"
    "636e7c0c0fe4eb0f697545460c806910d2c355f1d253bc9d2452aaa549e27a1f"
    "ac7cf4ed77f322e8fa894b6a83810a34b361901751a6f5eb65a0326e07de7c12"
    "16ccce2d0193f958bb3850a833f7ae432b65bc5a53975c155aa4bcb4f7b2c4e5"
    "4df16efaf6ddea94e2c50b4cd1dfe06017e0e9d02900cffe1935e0491d77ffb4"
    "fdf85290fdd893d577b1131a610ef6a5c32b2ee0293617a37cbb08b847741c3b"
    "8017c25ca9052ca1079d8b78aebd47876d330a30f6a8c6d61dd1ab5589329de7"
    "14d19d61370f8149748c72f132f0fc99f34d766c6938597040d8f9e2bb522ff9"
    "9c63a344d6a2ae8aa8e51b7b90a4a806105fcbca31506c446151adfeceb51b91"
    "abfe43960977c87471cf9ad4074d30e10d6a7f03c63bd5d4317f68ff325ba3bd"
    "80bf4dc8b52a0ba031758022eb025cdd770b44d6d6cf0670f4e990b22347a7db"
    "848265e3e5eb72dfe8299ad7481a408322cac55786e52f633b2fb6b614eaed18"
    "d703dd84045a274ae8bfa73379661388d6991fe39b0d93debb41700b41f90a15"
    "c4d526250235ddcd6776fc77bc97e7a417ebcb31600d01e57f32162a8560cacc"
    "7e27a096d37a1a86952ec71bd89a3e9a30a2a26162984d7740f81193e8238e61"
    "f6b5b984d4d3dfa033c1bb7e4f0037febf406d91c0dccf32acf423cfa1e70710"
    "10d3f270121b493ce85054ef58bada42310138fe081adb04e2bd901f2f13458b"
    "3d6758158197107c14ebb193230cd1157380aa79cae1374a7c1e5bbcb80ee23e"
    "06ebfde206bfb0fcbc0edc4ebec309661bdd908d532eb0c6adc38b7ca7331dce"
    "8dfce39ab71e7c32d318d136b6100671a1ae6a6600e3899f31f0eed19e3417d1"
    "34b90c9058f8632c798d4490da4987307cba922d61c39805d072b589bd52fdf1"
    "e86215c2d54e6670e07383a27bbffb5addf47d66aa85a0c6f9f32e59d85a44dd"
    "5d3b22dc2be80919b490437ae4f36a0ae55edf1d0b5cb4e9a3ecabee93dfc6e3"
    "8d209d0fa6536d27a5d6fbb17641cde27525d61093f1b28072d111b2b4ae5f89"
    "d5974ee12e5cf7d5da4d6a31123041f33e61407e76cffcdcfd7e19ba58cf4b53"
    "6f4c4938ae79324dc402894b44faf8afbab35282ab659d13c93f70412e85cb19"
    "9a37ddec600545473cfb5a05e08d0b209973b2172b4d21fb69745a262ccde96b"
    "a18b2faa745b6fe189cf772a9f84cbfc"
;


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

ByteView view(const std::vector<uint8_t>& v) { return ByteView(v.data(), v.size()); }

struct Crypto {
    uint64_t offset;
    std::vector<uint8_t> data;
};

// The ClientHello is the CRYPTO frame payload: type(1) offset(1) length(2) data.
std::vector<uint8_t> client_hello_from_frame(const std::vector<uint8_t>& frame) {
    return std::vector<uint8_t>(frame.begin() + 4, frame.end());
}

void put_varint2(std::vector<uint8_t>& out, uint64_t v) {
    out.push_back(static_cast<uint8_t>(0x40 | (v >> 8)));
    out.push_back(static_cast<uint8_t>(v));
}

// Test-only encryption: CTR + dummy tag, then header protection (mirror of the decrypt path).
std::vector<uint8_t> protect(QuicVersion ver, const std::vector<uint8_t>& dcid, uint32_t pn,
                             const std::vector<Crypto>& frames, bool with_ping_and_ack = false) {
    std::vector<uint8_t> payload;
    if (with_ping_and_ack) {
        payload.push_back(0x01);
        // ACK: largest=0, delay=0, range_count=1, first_range=0, gap=0, len=0
        const uint8_t ack[] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
        payload.insert(payload.end(), ack, ack + sizeof(ack));
    }
    for (const auto& f : frames) {
        payload.push_back(0x06);
        put_varint2(payload, f.offset);
        put_varint2(payload, f.data.size());
        payload.insert(payload.end(), f.data.begin(), f.data.end());
    }
    payload.insert(payload.end(), 40, 0x00);  // PADDING

    QuicInitialKeys keys{};
    REQUIRE(derive_client_initial_keys(ver, ByteView(dcid.data(), dcid.size()), keys));

    const uint8_t type_bits = (ver == QuicVersion::V2) ? 1 : 0;
    std::vector<uint8_t> pkt;
    pkt.push_back(static_cast<uint8_t>(0xc0 | (type_bits << 4) | 0x03));
    const uint32_t v = static_cast<uint32_t>(ver);
    for (int i = 3; i >= 0; --i) pkt.push_back(static_cast<uint8_t>(v >> (8 * i)));
    pkt.push_back(static_cast<uint8_t>(dcid.size()));
    pkt.insert(pkt.end(), dcid.begin(), dcid.end());
    pkt.push_back(0);  // scid len
    pkt.push_back(0);  // token len
    put_varint2(pkt, 4 + payload.size() + 16);
    const std::size_t pn_offset = pkt.size();
    for (int i = 3; i >= 0; --i) pkt.push_back(static_cast<uint8_t>(pn >> (8 * i)));

    uint8_t nonce[12];
    std::copy(keys.iv.begin(), keys.iv.end(), nonce);
    for (int i = 0; i < 4; ++i) nonce[8 + i] ^= static_cast<uint8_t>(pn >> (8 * (3 - i)));
    crypto::Aes128KeySchedule key_ks;
    crypto::aes128_expand_key(keys.key.data(), key_ks);
    crypto::aes128_ctr_xor(key_ks, nonce, 2, payload.data(), payload.size());
    pkt.insert(pkt.end(), payload.begin(), payload.end());
    pkt.insert(pkt.end(), 16, 0xaa);  // dummy tag

    crypto::Aes128KeySchedule hp_ks;
    crypto::aes128_expand_key(keys.hp.data(), hp_ks);
    uint8_t mask[16];
    crypto::aes128_encrypt_block(hp_ks, pkt.data() + pn_offset + 4, mask);
    pkt[0] ^= mask[0] & 0x0f;
    for (int i = 0; i < 4; ++i) pkt[pn_offset + i] ^= mask[1 + i];
    return pkt;
}

std::vector<uint8_t> dcid_a() { return from_hex("8394c8f03e515708"); }

struct Collected {
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> frames;
};

bool run_decrypt(const std::vector<uint8_t>& dgram, QuicInitialPacketInfo& info, Collected& c) {
    std::vector<uint8_t> scratch;
    return decrypt_initial_datagram(
        view(dgram), info,
        [&](uint64_t off, ByteView d) { c.frames.emplace_back(off, std::vector<uint8_t>(d.begin(), d.end())); },
        scratch);
}

using Clock = std::chrono::steady_clock;

} // namespace

TEST_CASE("l7 quic: derive_client_initial_keys v1 RFC 9001 A.1") {
    QuicInitialKeys k{};
    auto dcid = dcid_a();
    REQUIRE(derive_client_initial_keys(QuicVersion::V1, view(dcid), k));
    CHECK(std::vector<uint8_t>(k.key.begin(), k.key.end()) == from_hex("1f369613dd76d5467730efcbe3b1a22d"));
    CHECK(std::vector<uint8_t>(k.iv.begin(), k.iv.end()) == from_hex("fa044b2f42a3fd3b46fb255c"));
    CHECK(std::vector<uint8_t>(k.hp.begin(), k.hp.end()) == from_hex("9f50449e04a0e810283a1e9933adedd2"));
}

TEST_CASE("l7 quic: derive_client_initial_keys v2 RFC 9369 A.1") {
    QuicInitialKeys k{};
    auto dcid = dcid_a();
    REQUIRE(derive_client_initial_keys(QuicVersion::V2, view(dcid), k));
    CHECK(std::vector<uint8_t>(k.key.begin(), k.key.end()) == from_hex("8b1a0bc121284290a29e0971b5cd045d"));
    CHECK(std::vector<uint8_t>(k.iv.begin(), k.iv.end()) == from_hex("91f73e2351d8fa91660e909f"));
    CHECK(std::vector<uint8_t>(k.hp.begin(), k.hp.end()) == from_hex("45b95e15235d6f45a6b19cbcb0294ba9"));
}

TEST_CASE("l7 quic: decrypt RFC 9001 A.2 client Initial (v1)") {
    auto pkt = from_hex(kV1ProtectedPacket);
    auto frame = from_hex(kV1CryptoFrame);
    QuicInitialPacketInfo info;
    Collected c;
    REQUIRE(run_decrypt(pkt, info, c));
    CHECK(info.version == QuicVersion::V1);
    CHECK(info.dcid_len == 8);
    CHECK(std::vector<uint8_t>(info.dcid.begin(), info.dcid.begin() + 8) == dcid_a());
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].first == 0);
    CHECK(c.frames[0].second == client_hello_from_frame(frame));
    CHECK(c.frames[0].second[0] == 0x01);
    CHECK(c.frames[0].second[1] == 0x00);
}

TEST_CASE("l7 quic: decrypt RFC 9369 A.2 client Initial (v2)") {
    auto pkt = from_hex(kV2ProtectedPacket);
    auto frame = from_hex(kV2CryptoFrame);
    QuicInitialPacketInfo info;
    Collected c;
    REQUIRE(run_decrypt(pkt, info, c));
    CHECK(info.version == QuicVersion::V2);
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].first == 0);
    CHECK(c.frames[0].second == client_hello_from_frame(frame));
}

TEST_CASE("l7 quic: assembler reassembles CRYPTO across datagrams in reverse order") {
    auto hello = client_hello_from_frame(from_hex(kV1CryptoFrame));
    REQUIRE(hello.size() > 100);
    std::vector<uint8_t> tail(hello.begin() + 100, hello.end());
    std::vector<uint8_t> head(hello.begin(), hello.begin() + 100);
    auto d1 = protect(QuicVersion::V1, dcid_a(), 1, {{100, tail}});
    auto d2 = protect(QuicVersion::V1, dcid_a(), 2, {{0, head}});

    QuicCryptoAssembler asmb;
    auto now = Clock::now();
    CHECK(asmb.feed(view(d1), now).size() == 0);
    CHECK(asmb.size() == 1);
    ByteView full = asmb.feed(view(d2), now);
    REQUIRE(full.size() == hello.size());
    CHECK(std::vector<uint8_t>(full.begin(), full.end()) == hello);
    asmb.erase_last();
    CHECK(asmb.size() == 0);
}

TEST_CASE("l7 quic: assembler split at 600 across datagrams, v2") {
    // Use a larger synthetic stream so the 600 split is meaningful.
    std::vector<uint8_t> stream = client_hello_from_frame(from_hex(kV2CryptoFrame));
    while (stream.size() < 1500) stream.push_back(static_cast<uint8_t>(stream.size()));
    std::vector<uint8_t> a(stream.begin() + 600, stream.end());
    std::vector<uint8_t> b(stream.begin(), stream.begin() + 600);
    auto d1 = protect(QuicVersion::V2, dcid_a(), 1, {{600, a}}, true);
    auto d2 = protect(QuicVersion::V2, dcid_a(), 2, {{0, b}});
    QuicCryptoAssembler asmb;
    auto now = Clock::now();
    CHECK(asmb.feed(view(d1), now).size() == 0);
    ByteView full = asmb.feed(view(d2), now);
    REQUIRE(full.size() == stream.size());
    CHECK(std::vector<uint8_t>(full.begin(), full.end()) == stream);
}

TEST_CASE("l7 quic: shuffled CRYPTO frames inside one packet") {
    auto hello = client_hello_from_frame(from_hex(kV1CryptoFrame));
    std::vector<Crypto> frames = {
        {200, std::vector<uint8_t>(hello.begin() + 200, hello.end())},
        {0, std::vector<uint8_t>(hello.begin(), hello.begin() + 100)},
        {100, std::vector<uint8_t>(hello.begin() + 100, hello.begin() + 200)},
    };
    auto d = protect(QuicVersion::V1, dcid_a(), 7, frames, true);
    QuicCryptoAssembler asmb;
    ByteView full = asmb.feed(view(d), Clock::now());
    REQUIRE(full.size() == hello.size());
    CHECK(std::vector<uint8_t>(full.begin(), full.end()) == hello);
}

TEST_CASE("l7 quic: coalesced Initial packets are all decrypted") {
    auto p1 = protect(QuicVersion::V1, dcid_a(), 1, {{0, {1, 2, 3}}});
    auto p2 = protect(QuicVersion::V1, dcid_a(), 2, {{3, {4, 5}}});
    std::vector<uint8_t> dgram = p1;
    dgram.insert(dgram.end(), p2.begin(), p2.end());
    QuicInitialPacketInfo info;
    Collected c;
    REQUIRE(run_decrypt(dgram, info, c));
    REQUIRE(c.frames.size() == 2);
    CHECK(c.frames[1].first == 3);
}

TEST_CASE("l7 quic: robustness on truncated and corrupted input") {
    auto pkt = from_hex(kV1ProtectedPacket);
    QuicCryptoAssembler asmb;
    auto now = Clock::now();
    for (std::size_t n = 0; n <= pkt.size(); ++n) {
        std::vector<uint8_t> p(pkt.begin(), pkt.begin() + n);
        QuicInitialPacketInfo info;
        Collected c;
        bool ok = run_decrypt(p, info, c);
        if (n < 40) CHECK_FALSE(ok);
        asmb.feed(view(p), now);
    }
    for (std::size_t i = 0; i < 40; ++i) {
        for (uint8_t x : {0x01, 0x80, 0xff}) {
            std::vector<uint8_t> p = pkt;
            p[i] ^= x;
            QuicInitialPacketInfo info;
            Collected c;
            run_decrypt(p, info, c);
            asmb.feed(view(p), now);
        }
    }
    CHECK(true);
}

TEST_CASE("l7 quic: rejects non-QUIC, short header and unknown version") {
    QuicInitialPacketInfo info;
    Collected c;
    CHECK_FALSE(run_decrypt({}, info, c));
    CHECK_FALSE(run_decrypt(from_hex("1234567890abcdef0011223344556677"), info, c));
    // Short header packet (first bit clear).
    auto pkt = from_hex(kV1ProtectedPacket);
    auto sh = pkt;
    sh[0] = 0x40;
    CHECK_FALSE(run_decrypt(sh, info, c));
    // Unknown version.
    auto uv = pkt;
    uv[4] = 0x02;
    CHECK_FALSE(run_decrypt(uv, info, c));
    // Handshake (type 2) instead of Initial.
    auto hs = pkt;
    hs[0] = static_cast<uint8_t>((hs[0] & ~0x30) | 0x20);
    CHECK_FALSE(run_decrypt(hs, info, c));
    CHECK(c.frames.empty());

    QuicCryptoAssembler asmb;
    CHECK(asmb.feed(view(sh), Clock::now()).size() == 0);
    CHECK(asmb.feed(view(uv), Clock::now()).size() == 0);
    CHECK(asmb.size() == 0);
}

TEST_CASE("l7 quic: assembler evicts least recently used connection") {
    QuicCryptoAssembler asmb(2);
    auto now = Clock::now();
    auto d1 = protect(QuicVersion::V1, from_hex("0101010101010101"), 1, {{0, {1, 2, 3}}});
    auto d2 = protect(QuicVersion::V1, from_hex("0202020202020202"), 1, {{0, {1, 2, 3}}});
    auto d3 = protect(QuicVersion::V1, from_hex("0303030303030303"), 1, {{0, {1, 2, 3}}});
    CHECK(asmb.feed(view(d1), now).size() == 3);
    CHECK(asmb.feed(view(d2), now).size() == 3);
    CHECK(asmb.feed(view(d3), now).size() == 3);
    CHECK(asmb.size() == 2);
    // d1's connection was evicted: a follow-up fragment at offset 3 has no offset-0 data.
    auto d1b = protect(QuicVersion::V1, from_hex("0101010101010101"), 2, {{3, {4}}});
    CHECK(asmb.feed(view(d1b), now).size() == 0);
}

TEST_CASE("l7 quic: assembler drops expired connections") {
    QuicCryptoAssembler asmb(8, 8192, std::chrono::milliseconds(2000));
    auto t0 = Clock::now();
    auto d1 = protect(QuicVersion::V1, from_hex("0101010101010101"), 1, {{0, {1, 2, 3}}});
    auto d2 = protect(QuicVersion::V1, from_hex("0202020202020202"), 1, {{0, {1, 2, 3}}});
    CHECK(asmb.feed(view(d1), t0).size() == 3);
    CHECK(asmb.size() == 1);
    CHECK(asmb.feed(view(d2), t0 + std::chrono::seconds(3)).size() == 3);
    CHECK(asmb.size() == 1);
    // Old connection state is gone: a continuation does not extend the old prefix.
    auto d1b = protect(QuicVersion::V1, from_hex("0101010101010101"), 2, {{3, {4}}});
    CHECK(asmb.feed(view(d1b), t0 + std::chrono::seconds(3)).size() == 0);
}

TEST_CASE("l7 quic: overlapping and touching fragments merge correctly") {
    // Test that overlapping ranges merge (r.second >= ns && r.first <= ne).
    // Send fragments with explicit overlaps and touching boundaries.
    std::vector<uint8_t> data1{10, 11, 12};
    std::vector<uint8_t> data2{12, 13, 14};  // overlaps at offset 2
    std::vector<uint8_t> data3{14, 15, 16};  // touches at offset 4
    auto dcid = dcid_a();
    auto d1 = protect(QuicVersion::V1, dcid, 1, {{0, data1}});
    auto d2 = protect(QuicVersion::V1, dcid, 2, {{2, data2}});
    auto d3 = protect(QuicVersion::V1, dcid, 3, {{4, data3}});

    QuicCryptoAssembler asmb;
    auto now = Clock::now();
    // Feed in order, check single merged range.
    ByteView result1 = asmb.feed(view(d1), now);
    CHECK(result1.size() == 3);
    CHECK(result1[0] == 10);
    CHECK(result1[1] == 11);
    CHECK(result1[2] == 12);

    ByteView result2 = asmb.feed(view(d2), now);
    CHECK(result2.size() == 5);  // [0, 5) because overlapping and touching ranges merge
    CHECK(result2[0] == 10);
    CHECK(result2[1] == 11);
    CHECK(result2[2] == 12);
    CHECK(result2[3] == 13);
    CHECK(result2[4] == 14);

    ByteView result3 = asmb.feed(view(d3), now);
    CHECK(result3.size() == 7);  // [0, 7) fully merged
    CHECK(result3[4] == 14);
    CHECK(result3[5] == 15);
    CHECK(result3[6] == 16);
}

TEST_CASE("l7 quic: 200 one-byte fragments exceed cap and drop connection") {
    // Create 200 one-byte fragments at non-adjacent offsets (0, 2, 4, 6, ..., 398).
    // Each fragment is at a separate range, exceeding kMaxRangesPerConn (16).
    // Verify connection state is dropped and later packets fail to extend it.
    auto dcid = from_hex("aabbccddaabbccdd");
    auto now = Clock::now();

    QuicCryptoAssembler asmb;
    std::vector<Crypto> frames;
    for (int i = 0; i < 200; ++i) {
        uint64_t offset = static_cast<uint64_t>(i) * 2;  // 0, 2, 4, 6, ...
        std::vector<uint8_t> payload{static_cast<uint8_t>(i & 0xFF)};
        frames.push_back({offset, payload});
    }

    auto dgram = protect(QuicVersion::V1, dcid, 1, frames);
    ByteView result = asmb.feed(view(dgram), now);

    // Connection should have been dropped during feed because ranges exceeded cap.
    // Result should be empty since we don't have a contiguous [0, n) prefix.
    CHECK(result.size() == 0);
    CHECK(asmb.size() == 0);  // Connection was dropped.
}

TEST_CASE("l7 quic: assembler accepts new connection after dropping one") {
    auto dcid1 = from_hex("0101010101010101");
    auto dcid2 = from_hex("0202020202020202");
    auto now = Clock::now();

    QuicCryptoAssembler asmb(8, 8192, std::chrono::milliseconds(2000));

    // Feed a packet with 200 fragments for dcid1 (will be dropped).
    std::vector<Crypto> many_frames;
    for (int i = 0; i < 200; ++i) {
        many_frames.push_back({{static_cast<uint64_t>(i) * 2}, {static_cast<uint8_t>(i)}});
    }
    auto d1 = protect(QuicVersion::V1, dcid1, 1, many_frames);
    CHECK(asmb.feed(view(d1), now).size() == 0);
    CHECK(asmb.size() == 0);  // dcid1 dropped.

    // Feed a normal packet for dcid2: verifies assembler can still accept new connections.
    auto d2 = protect(QuicVersion::V1, dcid2, 1, {{0, {42, 43, 44}}});
    ByteView result = asmb.feed(view(d2), now);
    REQUIRE(result.size() == 3);
    CHECK(result[0] == 42);
    CHECK(result[1] == 43);
    CHECK(result[2] == 44);
    CHECK(asmb.size() == 1);  // dcid2 stored.

    // Verify dcid1 (after being dropped) doesn't produce stale state:
    // a follow-up packet for dcid1 at offset 3 still has no contiguous prefix from 0.
    auto d1b = protect(QuicVersion::V1, dcid1, 2, {{3, {99}}});
    CHECK(asmb.feed(view(d1b), now).size() == 0);  // No ClientHello: dcid1 never has [0, n)
    // dcid1 may be recreated as a new connection, but dcid2 is still there.
    CHECK(asmb.size() >= 1);
}
