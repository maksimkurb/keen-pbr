#include <doctest/doctest.h>

#include "../src/dns/dns_wire.hpp"
#include "../src/intercept/intercept_processor.hpp"
#include "../src/intercept/intercept_service.hpp"

#include <netinet/in.h>

#include <cerrno>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::nfnl;

namespace {

using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;

struct RecordedAdd {
    std::string set;
    uint8_t family;
    std::array<uint8_t, 16> addr;
    uint32_t timeout_s;
};

class FakeSetWriter : public DynamicSetWriter {
public:
    bool add(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        if (on_enter) on_enter();
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        last_timeout_ms = timeout_ms;
        ++calls;
        bool ok = true;
        for (std::size_t i = 0; i < count; ++i) {
            recorded.push_back({std::string(adds[i].set_name), adds[i].family, adds[i].addr,
                                adds[i].timeout_s});
            if (simulate_timeout) {
                out[i] = SetAddResult::Error;
                ok = false;
            } else {
                out[i] = result;
            }
        }
        errno_ = simulate_timeout ? ETIMEDOUT : 0;
        return ok;
    }
    int last_errno() const override { return errno_; }

    std::vector<RecordedAdd> recorded;
    SetAddResult result{SetAddResult::Added};
    bool simulate_timeout{false};
    int delay_ms{0};
    std::function<void()> on_enter;
    int last_timeout_ms{0};
    int calls{0};
    int errno_{0};
};

class FakeCleanup : public ConntrackCleanupSink {
public:
    void request(uint8_t family, const std::array<uint8_t, 16>& dst) override {
        requests.emplace_back(family, dst);
    }
    std::vector<std::pair<uint8_t, std::array<uint8_t, 16>>> requests;
};

void put16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}
void put32(Bytes& b, uint32_t v) {
    put16(b, static_cast<uint16_t>(v >> 16));
    put16(b, static_cast<uint16_t>(v & 0xFFFF));
}
void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }

Bytes enc(const std::string& name) {
    Bytes b;
    std::size_t start = 0;
    while (start < name.size()) {
        std::size_t dot = name.find('.', start);
        if (dot == std::string::npos) dot = name.size();
        b.push_back(static_cast<uint8_t>(dot - start));
        b.insert(b.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                 name.begin() + static_cast<std::ptrdiff_t>(dot));
        start = dot + 1;
    }
    b.push_back(0);
    return b;
}

struct Rr {
    uint16_t type;
    uint32_t ttl;
    Bytes rdata;
    Bytes owner;  // empty => pointer to question
};

Bytes dns_response(const std::string& qname, uint16_t rcode, const std::vector<Rr>& answers) {
    Bytes m;
    put16(m, 0x1234);
    put16(m, static_cast<uint16_t>(0x8180 | rcode));
    put16(m, 1);
    put16(m, static_cast<uint16_t>(answers.size()));
    put16(m, 0);
    put16(m, 0);
    append(m, enc(qname));
    put16(m, 1);
    put16(m, 1);
    for (const Rr& r : answers) {
        if (r.owner.empty()) {
            m.push_back(0xC0);
            m.push_back(0x0C);
        } else {
            append(m, r.owner);
        }
        put16(m, r.type);
        put16(m, 1);
        put32(m, r.ttl);
        put16(m, static_cast<uint16_t>(r.rdata.size()));
        append(m, r.rdata);
    }
    return m;
}

Rr a_rr(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint32_t ttl) {
    return Rr{1, ttl, {a, b, c, d}, {}};
}
Rr aaaa_rr(uint8_t last, uint32_t ttl) {
    Bytes r(16, 0);
    r[0] = 0x20;
    r[1] = 0x01;
    r[15] = last;
    return Rr{28, ttl, r, {}};
}

Bytes ipv4_header(uint8_t proto, std::size_t l4_len, const uint8_t src[4], const uint8_t dst[4]) {
    Bytes p(20, 0);
    p[0] = 0x45;
    const uint16_t total = static_cast<uint16_t>(20 + l4_len);
    p[2] = static_cast<uint8_t>(total >> 8);
    p[3] = static_cast<uint8_t>(total & 0xFF);
    p[8] = 55;
    p[9] = proto;
    for (int i = 0; i < 4; ++i) {
        p[12 + i] = src[i];
        p[16 + i] = dst[i];
    }
    return p;
}

Bytes dns_packet(const Bytes& payload) {
    const uint8_t src[4] = {8, 8, 8, 8};
    const uint8_t dst[4] = {192, 168, 1, 10};
    Bytes p = ipv4_header(IPPROTO_UDP, 8 + payload.size(), src, dst);
    put16(p, 53);
    put16(p, 40000);
    put16(p, static_cast<uint16_t>(8 + payload.size()));
    put16(p, 0);
    append(p, payload);
    return p;
}

constexpr uint8_t kClient[4] = {192, 168, 1, 10};
constexpr uint8_t kServer[4] = {203, 0, 113, 7};

Bytes tcp_packet(const Bytes& payload, uint32_t seq, uint16_t dport) {
    Bytes p = ipv4_header(IPPROTO_TCP, 20 + payload.size(), kClient, kServer);
    put16(p, 50000);
    put16(p, dport);
    put32(p, seq);
    put32(p, 0);
    p.push_back(0x50);  // data offset 5
    p.push_back(0x18);  // PSH|ACK
    put16(p, 65535);
    put16(p, 0);
    put16(p, 0);
    append(p, payload);
    return p;
}

Bytes udp_packet(const Bytes& payload, uint16_t dport) {
    Bytes p = ipv4_header(IPPROTO_UDP, 8 + payload.size(), kClient, kServer);
    put16(p, 50000);
    put16(p, dport);
    put16(p, static_cast<uint16_t>(8 + payload.size()));
    put16(p, 0);
    append(p, payload);
    return p;
}

ByteView view(const Bytes& b) { return ByteView(b.data(), b.size()); }

// --- TLS ClientHello helpers (minimal copy of tests/test_l7_tls.cpp) ---
Bytes build_server_name_extension(const std::string& sni) {
    Bytes r;
    put16(r, static_cast<uint16_t>(sni.size() + 3));
    r.push_back(0);
    put16(r, static_cast<uint16_t>(sni.size()));
    r.insert(r.end(), sni.begin(), sni.end());
    return r;
}

Bytes build_client_hello(const std::string& sni) {
    Bytes r;
    r.push_back(1);
    r.insert(r.end(), 3, 0);  // length placeholder
    r.push_back(3);
    r.push_back(3);
    for (int i = 0; i < 32; ++i) r.push_back(static_cast<uint8_t>(i));
    r.push_back(0);                  // session id len
    put16(r, 4);                     // cipher suites
    put16(r, 0x002F);
    put16(r, 0x002F);
    r.push_back(1);
    r.push_back(0);                  // compression
    Bytes ext;
    const Bytes sn = build_server_name_extension(sni);
    put16(ext, 0);
    put16(ext, static_cast<uint16_t>(sn.size()));
    append(ext, sn);
    put16(r, static_cast<uint16_t>(ext.size()));
    append(r, ext);
    const uint32_t body = static_cast<uint32_t>(r.size() - 4);
    r[1] = static_cast<uint8_t>(body >> 16);
    r[2] = static_cast<uint8_t>(body >> 8);
    r[3] = static_cast<uint8_t>(body);
    return r;
}

Bytes tls_stream(const std::string& sni) {
    const Bytes hello = build_client_hello(sni);
    Bytes s = {22, 3, 3, static_cast<uint8_t>(hello.size() >> 8), static_cast<uint8_t>(hello.size())};
    append(s, hello);
    return s;
}

Bytes from_hex(const std::string& hex) {
    Bytes out;
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

// RFC 9001 Appendix A.2 client Initial (SNI "example.com").
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

struct Fixture {
    FakeSetWriter writer;
    FakeCleanup cleanup;
    InterceptCounters counters;
    InterceptProcessor proc{writer, cleanup, counters};
    std::shared_ptr<InterceptSnapshot> snap;

    explicit Fixture(uint32_t min_ttl = 300) {
        DomainIndex::Builder b;
        const auto ex = b.add_list("ex");
        b.add_domain(ex, "example.com");
        const auto vid = b.add_list("vid");
        b.add_domain(vid, "video.example.com");
        const auto both = b.add_list("both");
        b.add_domain(both, "example.com");
        snap = std::make_shared<InterceptSnapshot>();
        snap->index = std::make_shared<DomainIndex>(std::move(b).build());
        snap->targets.resize(3);
        snap->targets[0] = {"kpbr4d_ex", "kpbr6d_ex", min_ttl};
        snap->targets[1] = {"kpbr4d_vid", "", min_ttl};
        snap->targets[2] = {"", "", min_ttl};
        proc.set_snapshot(snap);
    }
    void publish() { proc.set_snapshot(snap); }
    static Clock::time_point deadline() { return Clock::now() + std::chrono::milliseconds(30); }
};

} // namespace

TEST_CASE("intercept: DNS response adds A and AAAA with clamped timeouts") {
    Fixture f;
    const Bytes resp = dns_response("www.example.com", 0,
                                    {a_rr(93, 184, 216, 34, 60), a_rr(93, 184, 216, 35, 600),
                                     aaaa_rr(1, 600)});
    const Bytes pkt = dns_packet(resp);
    const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    CHECK_FALSE(d.replace);
    // example.com is in lists "ex" only (video.example.com is a different, longer domain).
    REQUIRE(f.writer.recorded.size() == 3);
    CHECK(f.writer.recorded[0].set == "kpbr4d_ex");
    CHECK(f.writer.recorded[0].timeout_s == 300);
    CHECK(f.writer.recorded[1].set == "kpbr4d_ex");
    CHECK(f.writer.recorded[1].timeout_s == 600);
    CHECK(f.writer.recorded[2].set == "kpbr6d_ex");
    CHECK(f.writer.recorded[2].family == 6);
    CHECK(f.writer.recorded[2].timeout_s == 600);
    CHECK(f.writer.last_timeout_ms >= 1);
    CHECK(f.writer.last_timeout_ms <= 30);
    CHECK(f.counters.dns_packets == 1);
    CHECK(f.counters.dns_matched == 1);
    CHECK(f.counters.set_added == 3);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].source == InterceptSource::dns);
    CHECK(events[0].domain == "www.example.com");
    CHECK(events[0].lists == std::vector<std::string>{"ex", "both"});
    CHECK(events[0].ips.size() == 3);
    CHECK(events[0].ips[0] == "93.184.216.34");
    CHECK(events[0].added == 3);
    CHECK_FALSE(events[0].timed_out);
}

TEST_CASE("intercept: CNAME target match") {
    Fixture f;
    Bytes target_owner = enc("cdn.example.com");
    const Bytes resp = dns_response(
        "unlisted.test", 0,
        {Rr{5, 100, enc("cdn.example.com"), {}}, Rr{1, 100, {1, 2, 3, 4}, target_owner}});
    const Bytes pkt = dns_packet(resp);
    f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    REQUIRE(f.writer.recorded.size() == 1);
    CHECK(f.writer.recorded[0].set == "kpbr4d_ex");
    CHECK(f.writer.recorded[0].timeout_s == 300);
}

TEST_CASE("intercept: no match, NXDOMAIN and malformed produce no adds") {
    Fixture f;
    const Bytes other = dns_packet(dns_response("other.org", 0, {a_rr(1, 1, 1, 1, 60)}));
    f.proc.on_dns_packet(view(other), Fixture::deadline(), true);
    CHECK(f.writer.calls == 0);
    CHECK(f.proc.events_since(0, 10).empty());

    const Bytes nx = dns_packet(dns_response("www.example.com", 3, {}));
    f.proc.on_dns_packet(view(nx), Fixture::deadline(), true);
    CHECK(f.writer.calls == 0);
    CHECK(f.proc.events_since(0, 10).empty());

    Bytes garbage = dns_packet(Bytes{1, 2, 3, 4, 5});
    f.proc.on_dns_packet(view(garbage), Fixture::deadline(), true);
    CHECK(f.counters.dns_parse_errors == 1);
    const Bytes tiny = {0x45, 0x00};
    f.proc.on_dns_packet(view(tiny), Fixture::deadline(), true);
    CHECK(f.writer.calls == 0);
}

TEST_CASE("intercept: TTL clamping") {
    {
        Fixture f(300);
        const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 5)}));
        f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
        REQUIRE(f.writer.recorded.size() == 1);
        CHECK(f.writer.recorded[0].timeout_s == 300);
    }
    {
        Fixture f(300);
        const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 999999)}));
        f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
        REQUIRE(f.writer.recorded.size() == 1);
        CHECK(f.writer.recorded[0].timeout_s == 86400);
    }
    {
        Fixture f(0);
        const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
        f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
        REQUIRE(f.writer.recorded.size() == 1);
        CHECK(f.writer.recorded[0].timeout_s == 0);
    }
}

TEST_CASE("intercept: domain in two lists adds to both sets, deduped") {
    Fixture f;
    f.snap->targets[2] = {"kpbr4d_both", "", 300};
    f.publish();
    // Same address twice: dedup by (set, ip).
    const Bytes pkt = dns_packet(
        dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100), a_rr(1, 2, 3, 4, 700)}));
    f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    REQUIRE(f.writer.recorded.size() == 2);
    CHECK(f.writer.recorded[0].set == "kpbr4d_ex");
    CHECK(f.writer.recorded[0].timeout_s == 700);
    CHECK(f.writer.recorded[1].set == "kpbr4d_both");
}

TEST_CASE("intercept: writer timeout counts and requests conntrack cleanup") {
    Fixture f;
    f.writer.simulate_timeout = true;
    const Bytes pkt = dns_packet(
        dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100), aaaa_rr(9, 100)}));
    f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    CHECK(f.counters.dns_hold_timeouts == 1);
    CHECK(f.counters.set_errors == 2);
    REQUIRE(f.cleanup.requests.size() == 2);
    CHECK(f.cleanup.requests[0].first == 4);
    CHECK(f.cleanup.requests[1].first == 6);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].timed_out);
}

TEST_CASE("intercept: expired DNS deadline accepts without a set write") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    CHECK(f.writer.calls == 0);
    CHECK(f.counters.dns_hold_timeouts == 1);
    CHECK(f.counters.set_errors == 0);
    CHECK(f.cleanup.requests.empty());
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].timed_out);
}

TEST_CASE("intercept: DNS deadline is checked again after writer admission") {
    Fixture f;
    f.proc.set_writer_callbacks(
        [] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return true;
        },
        {}, {}, {});
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.proc.on_dns_packet(view(pkt), Clock::now() + std::chrono::milliseconds(50), true);
    CHECK(f.writer.calls == 1);
    CHECK(f.writer.last_timeout_ms < 50);
    CHECK(f.writer.last_timeout_ms >= 1);
    CHECK(f.counters.dns_hold_timeouts == 0);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK_FALSE(events[0].timed_out);
}

TEST_CASE("intercept: marker replacement") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("check.keen.pbr", 3, {}));
    {
        const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
        REQUIRE(d.replace);
        REQUIRE(d.replacement != nullptr);
        const auto layout = dns_wire::parse_packet_layout(view(*d.replacement));
        REQUIRE(layout.has_value());
        dns_wire::ParsedResponse r;
        REQUIRE(dns_wire::parse_response(
            ByteView(d.replacement->data() + layout->payload_offset, layout->payload_len), r));
        REQUIRE(r.addresses.size() == 1);
        CHECK(r.addresses[0].family == 4);
        CHECK(r.addresses[0].addr[0] == 127);
        CHECK(r.addresses[0].addr[3] == 88);
    }
    {
        const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), false);
        CHECK_FALSE(d.replace);
    }
    CHECK(f.counters.marker_hits == 2);
    CHECK(f.writer.calls == 0);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 2);
    CHECK(events[0].source == InterceptSource::marker);
}

TEST_CASE("intercept: L7 TLS SNI split across two segments") {
    Fixture f;
    const Bytes stream = tls_stream("video.example.com");
    const std::size_t cut = 40;
    const Bytes first(stream.begin(), stream.begin() + static_cast<std::ptrdiff_t>(cut));
    const Bytes second(stream.begin() + static_cast<std::ptrdiff_t>(cut), stream.end());
    const auto now = Clock::now();
    const Bytes p1 = tcp_packet(first, 1000, 443);
    const Bytes p2 = tcp_packet(second, 1000 + static_cast<uint32_t>(cut), 443);

    f.proc.on_l7_packet(view(p1), now);
    CHECK(f.writer.calls == 0);
    f.proc.on_l7_packet(view(p2), now);
    // video.example.com is in "ex" (suffix) and "vid"; "both" has no sets.
    REQUIRE(f.writer.recorded.size() == 2);
    CHECK(f.writer.recorded[0].set == "kpbr4d_ex");
    CHECK(f.writer.recorded[1].set == "kpbr4d_vid");
    CHECK(f.writer.recorded[1].timeout_s == 300);
    CHECK(f.writer.recorded[1].addr[0] == 203);
    REQUIRE(f.cleanup.requests.size() == 1);
    CHECK(f.cleanup.requests[0].first == 4);
    CHECK(f.cleanup.requests[0].second[3] == 7);
    CHECK(f.counters.l7_matched == 1);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].source == InterceptSource::sni);
    CHECK(events[0].domain == "video.example.com");

    // Refreshed: no cleanup.
    f.writer.result = SetAddResult::Refreshed;
    const Bytes q1 = tcp_packet(first, 5000, 443);
    const Bytes q2 = tcp_packet(second, 5000 + static_cast<uint32_t>(cut), 443);
    f.proc.on_l7_packet(view(q1), now);
    f.proc.on_l7_packet(view(q2), now);
    CHECK(f.writer.recorded.size() == 4);
    CHECK(f.cleanup.requests.size() == 1);
}

TEST_CASE("intercept: L7 work can be handed to a bounded worker writer") {
    Fixture f;
    InterceptL7Work work;
    bool submitted = false;
    f.proc.set_l7_submitter([&](InterceptL7Work queued) {
        submitted = true;
        work = std::move(queued);
    });
    const Bytes pkt = tcp_packet(tls_stream("video.example.com"), 1, 443);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    CHECK(submitted);
    CHECK(f.writer.calls == 0);
    REQUIRE_FALSE(work.adds.empty());
    f.proc.process_l7_work(std::move(work), f.writer);
    CHECK(f.writer.calls == 1);
    CHECK(f.counters.set_added == 2);
    CHECK(f.cleanup.requests.size() == 1);
}

TEST_CASE("intercept: queued L7 work cannot write after snapshot invalidation") {
    Fixture f;
    InterceptL7Work work;
    f.proc.set_l7_submitter([&](InterceptL7Work queued) { work = std::move(queued); });
    const Bytes pkt = tcp_packet(tls_stream("video.example.com"), 1, 443);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    REQUIRE_FALSE(work.adds.empty());

    // The worker may already have taken ownership when a reapply invalidates
    // the old set snapshot.  It must reject that work instead of writing into
    // sets whose ownership/schema may have changed.
    f.proc.set_snapshot(nullptr);
    f.proc.process_l7_work(std::move(work), f.writer);
    CHECK(f.writer.calls == 0);
    CHECK(f.counters.set_added == 0);
    CHECK(f.counters.set_errors == 2);
    CHECK(f.cleanup.requests.empty());
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].errors == 2);
}

TEST_CASE("intercept: slow L7 writer does not block DNS writer") {
    Fixture f;
    InterceptL7Work work;
    f.proc.set_l7_submitter([&](InterceptL7Work queued) { work = std::move(queued); });
    const Bytes l7 = tcp_packet(tls_stream("video.example.com"), 1, 443);
    f.proc.on_l7_packet(view(l7), Clock::now());
    REQUIRE_FALSE(work.adds.empty());

    FakeSetWriter l7_writer;
    l7_writer.delay_ms = 100;
    f.writer.result = SetAddResult::Refreshed;
    std::mutex entered_mutex;
    std::condition_variable entered_cv;
    bool entered = false;
    l7_writer.on_enter = [&] {
        {
            std::lock_guard<std::mutex> lock(entered_mutex);
            entered = true;
        }
        entered_cv.notify_one();
    };
    std::thread worker([&] { f.proc.process_l7_work(std::move(work), l7_writer); });
    bool writer_entered = false;
    {
        std::unique_lock<std::mutex> lock(entered_mutex);
        writer_entered = entered_cv.wait_for(lock, std::chrono::seconds(1), [&] { return entered; });
    }
    CHECK(writer_entered);
    if (!writer_entered) {
        worker.join();
        return;
    }

    const auto started = Clock::now();
    const Bytes dns = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.proc.on_dns_packet(view(dns), Clock::now() + std::chrono::milliseconds(30), true);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
    worker.join();
    CHECK(f.writer.calls == 1);
    CHECK(l7_writer.calls == 1);
    CHECK(elapsed.count() < 80);
}

TEST_CASE("intercept: paused writers do not block service teardown") {
    Fixture f;
    auto writer = std::make_unique<FakeSetWriter>();
    InterceptService service(std::move(writer));
    service.start(InterceptServiceOptions{}, f.snap);
    {
        auto pause = service.pause_writes();
        service.stop();
        CHECK_FALSE(service.running());
    }
    CHECK_FALSE(service.running());
}

TEST_CASE("intercept: L7 HTTP Host") {
    Fixture f;
    const std::string req = "GET / HTTP/1.1\r\nHost: video.example.com\r\n\r\n";
    const Bytes pkt = tcp_packet(Bytes(req.begin(), req.end()), 1, 80);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    REQUIRE_FALSE(f.writer.recorded.empty());
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].source == InterceptSource::http);
    CHECK(f.cleanup.requests.size() == 1);
}

TEST_CASE("intercept: L7 QUIC Initial") {
    Fixture f;
    const Bytes datagram = from_hex(kV1ProtectedPacket);
    const Bytes pkt = udp_packet(datagram, 443);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].source == InterceptSource::quic);
    CHECK(events[0].domain == "example.com");
    REQUIRE_FALSE(f.writer.recorded.empty());
    CHECK(f.writer.recorded[0].set == "kpbr4d_ex");
}

TEST_CASE("intercept: L7 toggles disable protocols") {
    Fixture f;
    f.snap->tls = false;
    f.publish();
    const Bytes stream = tls_stream("video.example.com");
    const Bytes pkt = tcp_packet(stream, 1000, 443);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    CHECK(f.writer.calls == 0);
    CHECK(f.counters.l7_packets == 0);
}

TEST_CASE("intercept: events ring is bounded and monotonic") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("check.keen.pbr", 3, {}));
    for (int i = 0; i < 300; ++i) f.proc.on_dns_packet(view(pkt), Fixture::deadline(), false);
    const auto all = f.proc.events_since(0, 1000);
    REQUIRE(all.size() == 256);
    CHECK(all.front().seq == 45);
    CHECK(all.back().seq == 300);
    for (std::size_t i = 1; i < all.size(); ++i) CHECK(all[i].seq == all[i - 1].seq + 1);
    const auto tail = f.proc.events_since(295, 1000);
    CHECK(tail.size() == 5);
    CHECK(f.proc.events_since(0, 10).size() == 10);
}
