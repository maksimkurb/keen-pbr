#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/config/config.hpp"
#include "../src/dns/dns_wire.hpp"
#include "../src/firewall/firewall.hpp"
#include "../src/intercept/intercept_processor.hpp"
#include "../src/intercept/intercept_service.hpp"
#include "../src/intercept/intercept_snapshot_builder.hpp"
#include "../src/lists/list_streamer.hpp"

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
        kinds.push_back("add");
        return do_add(adds, out, count, timeout_ms);
    }
    bool add_new(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) override {
        kinds.push_back("add_new");
        if (exists_on_new) {
            ++calls;
            for (std::size_t i = 0; i < count; ++i) {
                recorded.push_back({std::string(adds[i].set_name), adds[i].family, adds[i].addr,
                                    adds[i].timeout_s});
                out[i] = SetAddResult::Exists;
            }
            errno_ = 0;
            return true;
        }
        return do_add(adds, out, count, timeout_ms);
    }
    bool refresh(const SetAdd* adds, SetAddResult* out, std::size_t count, int) override {
        kinds.push_back("refresh");
        ++refresh_calls;
        for (std::size_t i = 0; i < count; ++i) {
            refreshed.push_back({std::string(adds[i].set_name), adds[i].family, adds[i].addr,
                                 adds[i].timeout_s});
            out[i] = refresh_result;
        }
        errno_ = 0;
        return refresh_result != SetAddResult::Error;
    }

    bool do_add(const SetAdd* adds, SetAddResult* out, std::size_t count, int timeout_ms) {
        if (on_enter) on_enter();
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        last_timeout_ms = timeout_ms;
        ++calls;
        timeouts_ms.push_back(timeout_ms);
        const bool timeout_now = simulate_timeout || timeout_calls > 0;
        if (timeout_calls > 0) --timeout_calls;
        bool ok = true;
        for (std::size_t i = 0; i < count; ++i) {
            recorded.push_back({std::string(adds[i].set_name), adds[i].family, adds[i].addr,
                                adds[i].timeout_s});
            if (timeout_now) {
                out[i] = SetAddResult::Error;
                ok = false;
            } else if (real_error) {
                out[i] = SetAddResult::Error;
                ok = false;
            } else {
                out[i] = result;
            }
        }
        errno_ = timeout_now ? ETIMEDOUT : (real_error ? error_errno : 0);
        return ok;
    }
    int last_errno() const override { return errno_; }

    std::vector<RecordedAdd> recorded;
    std::vector<RecordedAdd> refreshed;
    std::vector<std::string> kinds;  // "add" / "add_new" / "refresh" in call order
    bool exists_on_new{false};
    SetAddResult refresh_result{SetAddResult::Refreshed};
    int refresh_calls{0};
    int error_errno{EPERM};
    SetAddResult result{SetAddResult::Added};
    bool simulate_timeout{false};
    int timeout_calls{0};  // the next N calls time out, later ones succeed
    bool real_error{false};
    std::vector<int> timeouts_ms;
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
    CHECK(events[0].hold_us >= events[0].parse_us + events[0].set_write_us);
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

TEST_CASE("intercept: no match and NXDOMAIN produce observations without adds") {
    Fixture f;
    const Bytes other = dns_packet(dns_response("other.org", 0, {a_rr(1, 1, 1, 1, 60)}));
    f.proc.on_dns_packet(view(other), Fixture::deadline(), true);
    CHECK(f.writer.calls == 0);
    const auto observed = f.proc.events_since(0, 10);
    REQUIRE(observed.size() == 1);
    CHECK(observed[0].domain == "other.org");
    CHECK(observed[0].client_ip == "192.168.1.10");
    CHECK(observed[0].ips == std::vector<std::string>{"1.1.1.1"});

    const Bytes nx = dns_packet(dns_response("www.example.com", 3, {}));
    f.proc.on_dns_packet(view(nx), Fixture::deadline(), true);
    CHECK(f.writer.calls == 0);
    CHECK(f.proc.events_since(0, 10).size() == 2);

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

TEST_CASE("intercept: persistent writer timeout is retried late, then counted as errors") {
    Fixture f;
    f.writer.simulate_timeout = true;
    const Bytes pkt = dns_packet(
        dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100), aaaa_rr(9, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    CHECK(d.late_write);
    CHECK(f.counters.dns_hold_timeouts == 1);
    CHECK(f.counters.set_errors == 0);  // unknown, not an error yet
    CHECK(f.proc.events_since(0, 10).empty());
    f.proc.flush_late_writes();
    CHECK(f.writer.calls == 2);
    CHECK(f.writer.timeouts_ms[1] == InterceptProcessor::kLateWriteBudgetMs);
    CHECK(f.counters.dns_late_writes == 1);
    CHECK(f.counters.dns_late_write_errors == 2);
    CHECK(f.counters.set_errors == 2);
    // Outcome unknown after ETIMEDOUT: purge flows anyway.
    REQUIRE(f.cleanup.requests.size() == 2);
    CHECK(f.cleanup.requests[0].first == 4);
    CHECK(f.cleanup.requests[1].first == 6);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].timed_out);
    CHECK(events[0].late_write);
    CHECK(events[0].errors == 2);
}

TEST_CASE("intercept: ETIMEDOUT then late success is Refreshed/Added, not an error") {
    Fixture f;
    f.writer.timeout_calls = 1;
    f.writer.result = SetAddResult::Refreshed;  // first attempt landed in the kernel
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    CHECK(d.late_write);
    f.proc.flush_late_writes();
    CHECK(f.counters.set_errors == 0);
    CHECK(f.counters.dns_late_write_errors == 0);
    CHECK(f.counters.set_refreshed == 1);
    CHECK(f.counters.dns_late_writes == 1);
    CHECK(f.cleanup.requests.size() == 1);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].late_write);
    CHECK(events[0].refreshed == 1);
    CHECK(events[0].errors == 0);

    Fixture g;
    g.writer.timeout_calls = 1;
    g.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    g.proc.flush_late_writes();
    CHECK(g.counters.set_added == 1);
    CHECK(g.counters.set_errors == 0);
    CHECK(g.cleanup.requests.size() == 1);
}

TEST_CASE("intercept: real writer error on time is a set error without late write") {
    Fixture f;
    f.writer.real_error = true;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    CHECK_FALSE(d.late_write);
    CHECK(f.counters.set_errors == 1);
    CHECK(f.counters.dns_hold_timeouts == 0);
    CHECK(f.cleanup.requests.empty());
    f.proc.flush_late_writes();  // no-op
    CHECK(f.writer.calls == 1);
}

TEST_CASE("intercept: expired DNS deadline accepts first, writes late with its own budget") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    CHECK(d.late_write);
    CHECK(f.writer.calls == 0);  // verdict can go out before any write
    CHECK(f.counters.dns_hold_timeouts == 1);
    CHECK(f.counters.set_errors == 0);
    CHECK(f.proc.events_since(0, 10).empty());
    f.proc.flush_late_writes();
    REQUIRE(f.writer.calls == 1);
    CHECK(f.writer.last_timeout_ms == InterceptProcessor::kLateWriteBudgetMs);
    CHECK(f.writer.recorded[0].set == "kpbr4d_ex");
    CHECK(f.counters.set_added == 1);
    CHECK(f.counters.dns_late_writes == 1);
    REQUIRE(f.cleanup.requests.size() == 1);  // Added late => reset stale flows
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].timed_out);
    CHECK(events[0].late_write);
    CHECK(events[0].added == 1);
}

TEST_CASE("intercept: late write refreshing an existing element does not purge flows") {
    Fixture f;
    f.writer.result = SetAddResult::Refreshed;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    f.proc.flush_late_writes();
    CHECK(f.counters.set_refreshed == 1);
    CHECK(f.cleanup.requests.empty());
}

TEST_CASE("intercept: late write is skipped when the snapshot changed") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    REQUIRE(d.late_write);
    f.proc.set_snapshot(nullptr);  // reload replaced the sets
    f.proc.flush_late_writes();
    CHECK(f.writer.calls == 0);
    CHECK(f.counters.dns_late_writes == 0);
    CHECK(f.counters.set_errors == 1);
    CHECK(f.counters.dns_late_write_errors == 1);
    CHECK(f.cleanup.requests.empty());
}

TEST_CASE("intercept: late write honours writer admission") {
    Fixture f;
    int released = 0;
    f.proc.set_writer_callbacks([] { return true; }, [&released] { ++released; }, {}, {});
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    CHECK(released == 0);  // nothing admitted before the verdict
    f.proc.flush_late_writes();
    CHECK(released == 1);
    CHECK(f.writer.calls == 1);

    Fixture g;
    g.proc.set_writer_callbacks([] { return false; }, [] {}, {}, {});
    g.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    g.proc.flush_late_writes();
    CHECK(g.writer.calls == 0);
    CHECK(g.counters.set_errors == 1);
}

TEST_CASE("intercept: back-to-back deadline misses release all verdicts before one combined write") {
    Fixture f;
    std::vector<std::string> order;
    f.writer.on_enter = [&order] { order.push_back("write"); };
    const Bytes p1 = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const Bytes p2 = dns_packet(dns_response("example.com", 0, {a_rr(5, 6, 7, 8, 100)}));
    const auto past = Clock::now() - std::chrono::milliseconds(70);
    for (const Bytes* p : {&p1, &p2}) {
        const auto d = f.proc.on_dns_packet(view(*p), past, true);
        CHECK(d.late_write);
        order.push_back("verdict");  // the service sends the verdict right here
    }
    CHECK(f.writer.calls == 0);
    CHECK(f.proc.pending_late_events() == 2);
    f.proc.flush_late_writes();
    REQUIRE(order.size() == 3);
    CHECK(order[0] == "verdict");
    CHECK(order[1] == "verdict");
    CHECK(order[2] == "write");
    CHECK(f.writer.calls == 1);  // one combined writer call
    CHECK(f.writer.recorded.size() == 2);
    CHECK(f.writer.last_timeout_ms == InterceptProcessor::kLateWriteBudgetMs);
    CHECK(f.counters.dns_hold_timeouts == 2);
    CHECK(f.counters.set_added == 2);
    CHECK(f.cleanup.requests.size() == 2);
    CHECK(f.proc.events_since(0, 10).size() == 2);
    CHECK(f.proc.pending_late_events() == 0);
    f.proc.flush_late_writes();  // nothing pending: no second write
    CHECK(f.writer.calls == 1);
}

TEST_CASE("intercept: a flush that timed out backs the next flush off to 50 ms") {
    Fixture f;
    f.writer.simulate_timeout = true;
    const Bytes p = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto past = Clock::now() - std::chrono::milliseconds(70);
    f.proc.on_dns_packet(view(p), past, true);
    f.proc.flush_late_writes();
    CHECK(f.writer.last_timeout_ms == InterceptProcessor::kLateWriteBudgetMs);
    f.proc.on_dns_packet(view(p), past, true);
    f.proc.flush_late_writes();
    CHECK(f.writer.last_timeout_ms == InterceptProcessor::kLateBackoffBudgetMs);
    CHECK(f.counters.dns_late_write_errors == 2);
    CHECK(f.counters.set_errors == 2);
}

TEST_CASE("intercept: pending-late batch overflow is counted and dropped") {
    Fixture f;
    const Bytes p = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto past = Clock::now() - std::chrono::milliseconds(70);
    for (std::size_t i = 0; i < InterceptProcessor::kLateBatchCapacity; ++i) {
        CHECK(f.proc.on_dns_packet(view(p), past, true).late_write);
    }
    const auto d = f.proc.on_dns_packet(view(p), past, true);
    CHECK_FALSE(d.late_write);
    CHECK(f.counters.dns_late_write_errors == 1);
    CHECK(f.counters.set_errors == 1);
    CHECK(f.proc.pending_late_events() == InterceptProcessor::kLateBatchCapacity);
    f.proc.flush_late_writes();
    CHECK(f.writer.calls == 1);
    CHECK(f.writer.recorded.size() == InterceptProcessor::kLateBatchCapacity);
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

    // Refreshed: no cleanup.  (A cached element would skip the write entirely.)
    f.proc.invalidate_set_cache();
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
    CHECK(events[0].client_ip == "192.168.1.10");
    CHECK(f.cleanup.requests.size() == 1);
}

TEST_CASE("intercept: unmatched L7 Host is observed without set work") {
    Fixture f;
    const std::string req = "GET / HTTP/1.1\r\nHost: other.org\r\n\r\n";
    const Bytes pkt = tcp_packet(Bytes(req.begin(), req.end()), 1, 80);
    f.proc.on_l7_packet(view(pkt), Clock::now());

    CHECK(f.writer.calls == 0);
    CHECK(f.cleanup.requests.empty());
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].source == InterceptSource::http);
    CHECK(events[0].client_ip == "192.168.1.10");
    CHECK(events[0].domain == "other.org");
    CHECK(events[0].lists.empty());
    CHECK(events[0].ips == std::vector<std::string>{"203.0.113.7"});
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
    const int total = static_cast<int>(InterceptProcessor::kEventCapacity) + 44;
    for (int i = 0; i < total; ++i) f.proc.on_dns_packet(view(pkt), Fixture::deadline(), false);
    const auto all = f.proc.events_since(0, 1000);
    REQUIRE(all.size() == InterceptProcessor::kEventCapacity);
    CHECK(all.front().seq == 45);
    CHECK(all.back().seq == static_cast<uint64_t>(total));
    for (std::size_t i = 1; i < all.size(); ++i) CHECK(all[i].seq == all[i - 1].seq + 1);
    const auto tail = f.proc.events_since(total - 5, 1000);
    CHECK(tail.size() == 5);
    CHECK(f.proc.events_since(0, 10).size() == 10);
}

// ---------------------------------------------------------------- set cache

namespace {

struct CacheFixture : Fixture {
    Clock::time_point fake_now{std::chrono::hours(1)};
    CacheFixture() {
        proc.set_clock([this] { return fake_now; });
    }
    void advance_ms(int64_t ms) { fake_now += std::chrono::milliseconds(ms); }
    void dns(const Bytes& pkt) { proc.on_dns_packet(view(pkt), deadline(), true); }
    std::vector<InterceptEvent> events() const { return proc.events_since(0, 1000); }
};

} // namespace

TEST_CASE("intercept cache: first response writes before the verdict and fills the cache") {
    CacheFixture f;
    f.dns(dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)})));
    REQUIRE(f.writer.kinds.size() == 1);
    CHECK(f.writer.kinds[0] == "add_new");  // pre-verdict probe, not the full add()
    CHECK(f.counters.set_cache_misses == 1);
    CHECK(f.counters.set_cache_hits == 0);
    CHECK(f.counters.set_cache_entries == 1);
    CHECK(f.proc.pending_refreshes() == 0);
    const auto events = f.events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].added == 1);
    CHECK(events[0].cache_hits == 0);
}

TEST_CASE("intercept cache: identical second response has zero writer calls before the verdict") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.dns(pkt);
    REQUIRE(f.writer.calls == 1);
    f.advance_ms(10000);
    // An already-expired hold deadline must not matter: nothing needs a write.
    const auto d = f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(5), true);
    CHECK_FALSE(d.late_write);
    CHECK(f.writer.calls == 1);
    CHECK(f.proc.pending_refreshes() == 0);  // within the 60 s refresh slack
    CHECK(f.counters.dns_hold_timeouts == 0);
    CHECK(f.counters.set_cache_hits == 1);
    CHECK(f.counters.dns_refresh_deferred == 0);
    const auto events = f.events();
    REQUIRE(events.size() == 2);
    CHECK(events[1].cache_hits == 1);
    CHECK(events[1].deferred_refresh == 0);
    CHECK(events[1].added == 0);
    CHECK_FALSE(events[1].timed_out);
    f.proc.flush_late_writes();
    CHECK(f.writer.calls == 1);
}

TEST_CASE("intercept cache: mixed response writes only the new address before the verdict") {
    CacheFixture f;
    std::vector<Rr> seven;
    for (uint8_t i = 1; i <= 7; ++i) seven.push_back(a_rr(10, 0, 0, i, 100));
    f.dns(dns_packet(dns_response("example.com", 0, seven)));
    REQUIRE(f.writer.recorded.size() == 7);
    f.writer.recorded.clear();
    f.writer.kinds.clear();
    std::vector<Rr> eight = seven;
    eight.push_back(a_rr(10, 0, 0, 99, 100));
    f.dns(dns_packet(dns_response("example.com", 0, eight)));
    REQUIRE(f.writer.recorded.size() == 1);
    CHECK(f.writer.recorded[0].addr[3] == 99);
    CHECK(f.writer.kinds == std::vector<std::string>{"add_new"});
    const auto events = f.events();
    CHECK(events.back().cache_hits == 7);
    CHECK(events.back().added == 1);
}

TEST_CASE("intercept cache: a due refresh waits for the verdict and goes through refresh()") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 86400)}));
    f.dns(pkt);
    f.writer.kinds.clear();
    // Past the trust bound (300 s) the entry is Stale: verdict is immediate,
    // the rewrite happens afterwards.
    f.advance_ms(SetElementCache::kMaxTrustMs + 1000);
    f.dns(pkt);
    CHECK(f.writer.kinds.empty());
    CHECK(f.proc.pending_refreshes() == 1);
    CHECK(f.counters.dns_refresh_deferred == 1);
    CHECK(f.events().back().deferred_refresh == 1);
    f.proc.flush_late_writes();
    CHECK(f.writer.kinds == std::vector<std::string>{"refresh"});
    REQUIRE(f.writer.refreshed.size() == 1);
    CHECK(f.writer.refreshed[0].set == "kpbr4d_ex");
    CHECK(f.proc.pending_refreshes() == 0);
    CHECK(f.counters.set_errors == 0);
    // The refresh renewed the cache entry: the next answer is a plain hit.
    f.dns(pkt);
    CHECK(f.proc.pending_refreshes() == 0);
}

TEST_CASE("intercept cache: remaining within 40% of TTL still skips refresh") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.dns(pkt);  // expires at +100 s (half TTL = 50 s)
    REQUIRE(f.writer.calls == 1);
    f.advance_ms(50000);  // Now at 50 s: remaining = 50 s == half TTL → no refresh yet
    f.dns(pkt);
    CHECK(f.proc.pending_refreshes() == 0);  // remaining == half TTL, not <, so no refresh
    CHECK(f.counters.refresh_skipped == 1);
}

TEST_CASE("intercept cache: new snapshot and invalidate_set_cache force pre-verdict writes") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.dns(pkt);
    f.dns(pkt);
    CHECK(f.writer.calls == 1);
    f.proc.invalidate_set_cache();
    CHECK(f.counters.set_cache_entries == 0);
    f.dns(pkt);
    CHECK(f.writer.calls == 2);
    f.dns(pkt);
    CHECK(f.writer.calls == 2);
    f.publish();  // new snapshot installed
    f.dns(pkt);
    CHECK(f.writer.calls == 3);
}

TEST_CASE("intercept cache: ENOENT from a write clears the cache") {
    CacheFixture f;
    const Bytes known = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.dns(known);
    f.writer.real_error = true;
    f.writer.error_errno = ENOENT;
    f.dns(dns_packet(dns_response("example.com", 0, {a_rr(5, 6, 7, 8, 100)})));
    CHECK(f.counters.set_errors == 1);
    CHECK(f.counters.set_cache_entries == 0);
    f.writer.real_error = false;
    const int calls = f.writer.calls;
    f.dns(known);  // forgotten: written again before the verdict
    CHECK(f.writer.calls == calls + 1);
}

TEST_CASE("intercept cache: a failed write is never cached") {
    CacheFixture f;
    f.writer.real_error = true;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.dns(pkt);
    CHECK(f.counters.set_cache_entries == 0);
    f.writer.real_error = false;
    f.dns(pkt);
    CHECK(f.writer.calls == 2);
}

TEST_CASE("intercept cache: EEXIST on a pre-verdict add moves the element to the refresh list") {
    CacheFixture f;
    f.writer.exists_on_new = true;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.dns(pkt);
    CHECK(f.writer.kinds == std::vector<std::string>{"add_new"});
    CHECK(f.counters.set_errors == 0);
    CHECK(f.proc.pending_refreshes() == 1);
    const auto events = f.events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].errors == 0);
    CHECK(events[0].added == 0);
    CHECK(events[0].refreshed == 1);
    CHECK(events[0].deferred_refresh == 1);
    CHECK(f.cleanup.requests.empty());
    f.proc.flush_late_writes();
    CHECK(f.writer.kinds == (std::vector<std::string>{"add_new", "refresh"}));
    // After the refresh the element is cached.
    f.writer.exists_on_new = false;
    f.dns(pkt);
    CHECK(f.writer.calls == 1);
}

TEST_CASE("intercept cache: Exists after a missed deadline is not retried as a late add") {
    CacheFixture f;
    f.writer.exists_on_new = true;
    f.writer.simulate_timeout = false;
    // Two addresses: both exist, none unconfirmed => no late write, only a refresh.
    const Bytes pkt = dns_packet(
        dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100), a_rr(5, 6, 7, 8, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    CHECK_FALSE(d.late_write);
    CHECK(f.proc.pending_refreshes() == 2);
    CHECK(f.proc.pending_late_events() == 0);
}

TEST_CASE("intercept cache: refresh overflow is dropped and counted, not an error") {
    CacheFixture f;
    constexpr int kPackets = 6;
    constexpr int kPerPacket = 100;
    std::vector<Bytes> packets;
    for (int p = 0; p < kPackets; ++p) {
        std::vector<Rr> rrs;
        for (int i = 0; i < kPerPacket; ++i) {
            rrs.push_back(a_rr(10, static_cast<uint8_t>(p), static_cast<uint8_t>(i), 1, 86400));
        }
        packets.push_back(dns_packet(dns_response("example.com", 0, rrs)));
    }
    for (const Bytes& p : packets) f.dns(p);
    CHECK(f.counters.set_cache_entries == kPackets * kPerPacket);
    f.advance_ms(SetElementCache::kMaxTrustMs + 1000);
    for (const Bytes& p : packets) f.dns(p);
    const int total = kPackets * kPerPacket;
    CHECK(f.proc.pending_refreshes() == InterceptProcessor::kLateBatchCapacity);
    CHECK(f.counters.refresh_dropped == static_cast<uint64_t>(total) - InterceptProcessor::kLateBatchCapacity);
    CHECK(f.counters.set_errors == 0);
    f.proc.flush_late_writes();
    CHECK(f.writer.refresh_calls == 1);
    CHECK(f.writer.refreshed.size() == InterceptProcessor::kLateBatchCapacity);
    CHECK(f.counters.set_errors == 0);
}

TEST_CASE("intercept cache: refresh of a vanished element counts as Added and purges flows") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 86400)}));
    f.dns(pkt);
    f.advance_ms(SetElementCache::kMaxTrustMs + 1000);
    f.dns(pkt);
    f.writer.refresh_result = SetAddResult::Added;
    f.proc.flush_late_writes();
    CHECK(f.cleanup.requests.size() == 1);
    CHECK(f.counters.set_added == 2);
}

TEST_CASE("intercept cache: failed refresh counts a set error") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 86400)}));
    f.dns(pkt);
    f.advance_ms(SetElementCache::kMaxTrustMs + 1000);
    f.dns(pkt);
    f.writer.refresh_result = SetAddResult::Error;
    f.proc.flush_late_writes();
    CHECK(f.counters.set_errors == 1);
}

TEST_CASE("intercept cache: queued refresh is dropped when the snapshot changed") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 86400)}));
    f.dns(pkt);
    f.advance_ms(SetElementCache::kMaxTrustMs + 1000);
    f.dns(pkt);
    REQUIRE(f.proc.pending_refreshes() == 1);
    f.proc.set_snapshot(nullptr);
    f.proc.flush_late_writes();
    CHECK(f.writer.refresh_calls == 0);
    CHECK(f.proc.pending_refreshes() == 0);
    CHECK(f.counters.set_errors == 0);
}

TEST_CASE("intercept cache: late write feeds the cache") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Clock::now() - std::chrono::milliseconds(1), true);
    REQUIRE(d.late_write);
    f.proc.flush_late_writes();
    CHECK(f.writer.kinds == std::vector<std::string>{"add"});  // late path keeps the full add()
    CHECK(f.counters.set_cache_entries == 1);
    f.dns(pkt);
    CHECK(f.writer.calls == 1);
}

TEST_CASE("intercept cache: L7 skips the write and the conntrack cleanup for a cached element") {
    CacheFixture f;
    std::vector<InterceptL7Work> queued;
    f.proc.set_l7_submitter([&](InterceptL7Work work) { queued.push_back(std::move(work)); });
    const Bytes pkt = tcp_packet(tls_stream("video.example.com"), 1, 443);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    REQUIRE(queued.size() == 1);
    f.proc.process_l7_work(std::move(queued[0]), f.writer);
    queued.clear();
    CHECK(f.writer.calls == 1);
    CHECK(f.cleanup.requests.size() == 1);

    f.proc.on_l7_packet(view(pkt), Clock::now());
    CHECK(queued.empty());  // nothing to write
    CHECK(f.writer.calls == 1);
    CHECK(f.cleanup.requests.size() == 1);
    const auto events = f.events();
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().cache_hits == 2);
    CHECK(events.back().added == 0);

    // After a trust-age expiry the L7 path rewrites like before.
    f.advance_ms(SetElementCache::kMaxTrustMs + 1000);
    f.proc.invalidate_set_cache();
    f.proc.on_l7_packet(view(pkt), Clock::now());
    CHECK(queued.size() == 1);
}

TEST_CASE("intercept cache: L7 record from before an invalidation is discarded") {
    CacheFixture f;
    std::vector<InterceptL7Work> queued;
    f.proc.set_l7_submitter([&](InterceptL7Work work) { queued.push_back(std::move(work)); });
    const Bytes pkt = tcp_packet(tls_stream("video.example.com"), 1, 443);
    f.proc.on_l7_packet(view(pkt), Clock::now());
    REQUIRE(queued.size() == 1);
    f.proc.invalidate_set_cache();  // sets recreated while the work waited in the queue
    f.proc.process_l7_work(std::move(queued[0]), f.writer);
    CHECK(f.counters.set_cache_entries == 0);
}

TEST_CASE("intercept cache: three identical responses within half TTL yield one total write") {
    CacheFixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    // First response: writes before verdict (Unknown).
    f.dns(pkt);
    REQUIRE(f.writer.calls == 1);
    REQUIRE(f.counters.dns_refresh_deferred == 0);
    REQUIRE(f.counters.refresh_skipped == 0);
    // 30 s later (half TTL = 50 s): remaining 70 s > 50 s, no refresh needed.
    f.advance_ms(30000);
    f.dns(pkt);
    CHECK(f.writer.calls == 1);  // no new write
    CHECK(f.counters.dns_refresh_deferred == 0);
    CHECK(f.counters.refresh_skipped == 1);
    // 40 s later (total 70 s): remaining 30 s < 50 s, refresh is skipped too.
    f.advance_ms(10000);
    f.dns(pkt);
    CHECK(f.writer.calls == 1);  // still no new write
    CHECK(f.counters.dns_refresh_deferred == 0);
    CHECK(f.counters.refresh_skipped == 2);
    // After flushing, there's nothing to do.
    f.proc.flush_late_writes();
    CHECK(f.writer.calls == 1);
}

// ---- timeout diagnostics (timing breakdown, cause, histograms) ----

TEST_CASE("intercept diag: deadline already passed is budget_spent_by_batch") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto woke = Clock::now() - std::chrono::milliseconds(60);
    const auto deadline = woke + std::chrono::milliseconds(30);
    const auto d = f.proc.on_dns_packet(view(pkt), DnsRound(woke, deadline, 3), true);
    REQUIRE(d.late_write);
    f.proc.flush_late_writes();
    f.proc.set_round_batch_size(1, 7);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    const InterceptEvent& e = events[0];
    CHECK(e.timed_out);
    CHECK(e.timeout_cause == TimeoutCause::budget_spent_by_batch);
    CHECK(e.batch_pos == 3);
    CHECK(e.batch_size == 7);
    CHECK(e.queue_wait_us >= 60000);
    CHECK(e.budget_left_us <= -30000);
    CHECK(e.queue_wait_us + e.budget_left_us == doctest::Approx(30000).epsilon(0.01));  // = hold
    CHECK(e.write_elements == 0);  // never attempted before the verdict
    CHECK(e.late_batch_elements == 1);
    CHECK(f.counters.dns_timeout_budget_spent_by_batch == 1);
    CHECK(f.counters.dns_timeout_admission_blocked == 0);
    CHECK(f.counters.late_write_latency.buckets[0] == 1);
    CHECK(f.counters.late_write_latency.max_elements == 1);
    CHECK(f.counters.dns_write_latency.buckets[0] == 0);
}

TEST_CASE("intercept diag: admission blocking past the deadline is admission_blocked") {
    Fixture f;
    f.proc.set_writer_callbacks(
        [] { std::this_thread::sleep_for(std::chrono::milliseconds(15)); return true; }, [] {}, {}, {});
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto woke = Clock::now();
    const auto d = f.proc.on_dns_packet(
        view(pkt), DnsRound(woke, woke + std::chrono::milliseconds(5), 0), true);
    REQUIRE(d.late_write);
    f.proc.flush_late_writes();
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].timeout_cause == TimeoutCause::admission_blocked);
    CHECK(events[0].admission_wait_us >= 10000);
    CHECK(events[0].budget_left_us > 0);  // started in time, lost it waiting
    CHECK(f.counters.dns_timeout_admission_blocked == 1);
}

TEST_CASE("intercept diag: own write ETIMEDOUT is own_write_slow") {
    Fixture f;
    f.writer.timeout_calls = 1;
    f.writer.delay_ms = 2;
    const Bytes pkt = dns_packet(
        dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100), aaaa_rr(9, 100)}));
    const auto d = f.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    REQUIRE(d.late_write);
    f.proc.flush_late_writes();
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK(events[0].timeout_cause == TimeoutCause::own_write_slow);
    CHECK(events[0].write_elements == 2);
    CHECK(events[0].late_batch_elements == 2);
    CHECK(events[0].write_errno == ETIMEDOUT);  // the on-time attempt's errno; the late retry succeeded
    CHECK(f.counters.dns_timeout_own_write_slow == 1);
    CHECK(f.counters.dns_write_latency.buckets[1] == 1);  // 2 ms sync write
    CHECK(f.counters.dns_write_latency.max_elements == 2);
    CHECK(f.counters.dns_write_latency.max_us >= 2000);

    Fixture g;
    g.writer.simulate_timeout = true;
    g.proc.on_dns_packet(view(pkt), Fixture::deadline(), true);
    g.proc.flush_late_writes();
    const auto ge = g.proc.events_since(0, 10);
    REQUIRE(ge.size() == 1);
    CHECK(ge[0].write_errno == ETIMEDOUT);
}

TEST_CASE("intercept diag: full late batch is late_batch_full") {
    Fixture f;
    const Bytes p = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    const auto past = Clock::now() - std::chrono::milliseconds(70);
    for (std::size_t i = 0; i < InterceptProcessor::kLateBatchCapacity; ++i) {
        f.proc.on_dns_packet(view(p), past, true);
    }
    f.proc.on_dns_packet(view(p), past, true);
    CHECK(f.counters.dns_timeout_budget_spent_by_batch == InterceptProcessor::kLateBatchCapacity);
    CHECK(f.counters.dns_timeout_late_batch_full == 1);
    CHECK(f.counters.dns_hold_timeouts == InterceptProcessor::kLateBatchCapacity + 1);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);  // only the dropped one is published immediately
    CHECK(events[0].timeout_cause == TimeoutCause::late_batch_full);
    CHECK(events[0].errors == 1);
}

TEST_CASE("intercept diag: on-time write fills the timing fields without a timeout") {
    Fixture f;
    const Bytes pkt = dns_packet(dns_response("example.com", 0, {a_rr(1, 2, 3, 4, 100)}));
    f.proc.on_dns_packet(view(pkt), DnsRound(Clock::now(), Clock::now() + std::chrono::seconds(5), 0), true);
    const auto events = f.proc.events_since(0, 10);
    REQUIRE(events.size() == 1);
    CHECK_FALSE(events[0].timed_out);
    CHECK(events[0].timeout_cause == TimeoutCause::none);
    CHECK(events[0].batch_pos == 0);
    CHECK(events[0].write_elements == 1);
    CHECK(events[0].write_errno == 0);
    CHECK(events[0].budget_left_us > 4000000);
    CHECK(f.counters.dns_write_latency.buckets[0] == 1);
    CHECK(f.counters.dns_hold_timeouts == 0);
}

TEST_CASE("intercept diag: write latency buckets and max") {
    WriteLatencyCounters h;
    h.record(999, 1);
    h.record(1000, 2);
    h.record(5000, 3);
    h.record(10000, 4);
    h.record(30000, 5);
    h.record(100000, 6);
    h.record(50, 99);
    for (std::size_t i = 0; i < WriteLatencyCounters::kBuckets; ++i) {
        CHECK(h.buckets[i] == (i == 0 ? 2u : 1u));
    }
    CHECK(h.max_us == 100000);
    CHECK(h.max_elements == 6);
}

namespace {

// Snapshot-rebind scenario: the previous apply published a full snapshot; the
// new apply keeps the DomainIndex and only re-derives the set bindings.
struct RebindFixture {
    FakeSetWriter writer;
    FakeCleanup cleanup;
    InterceptCounters counters;
    InterceptProcessor proc{writer, cleanup, counters};
    CacheManager cache{"/nonexistent/cache"};
    ListStreamer streamer{cache};
    InterceptEffective effective;

    static std::vector<FirewallSetDeclaration> sets_for(const std::vector<std::string>& names) {
        std::vector<FirewallSetDeclaration> sets;
        for (const auto& name : names) {
            sets.push_back({Firewall::dynamic_set_name(name, AF_INET), FirewallFamily::ipv4, 0});
        }
        return sets;
    }
    static Config config_with(const std::string& lists, const std::string& rule_lists) {
        return parse_config(R"({"lists":)" + lists + R"(,"route":{"rules":[{"list":)" +
                            rule_lists + R"(,"outbound":"wan"}]}})");
    }
    std::shared_ptr<const InterceptSnapshot> rebind(const Config& config,
                                                    const std::vector<std::string>& declared) {
        const auto previous = proc.current_snapshot();
        const auto bindings = build_intercept_bindings(config, sets_for(declared), false, effective);
        return rebind_intercept_snapshot(previous.get(), bindings, effective);
    }
    void answer(const std::string& name) {
        const Bytes pkt = dns_packet(dns_response(name, 0, {a_rr(1, 2, 3, 4, 100)}));
        proc.on_dns_packet(view(pkt), Clock::now() + std::chrono::milliseconds(30), true);
    }
};

const char* const kTwoLists = R"({"a":{"domains":["a.example"]},"b":{"domains":["b.example"]}})";

} // namespace

TEST_CASE("intercept rebind: answers before the background build are learned") {
    RebindFixture f;
    const Config config = RebindFixture::config_with(kTwoLists, R"(["a","b"])");
    f.proc.set_snapshot(build_intercept_snapshot(config, RebindFixture::sets_for({"a", "b"}), false,
                                                 f.effective, f.streamer));
    // Apply recreates the sets; the index is not rebuilt yet.
    f.proc.invalidate_set_cache();
    const auto rebound = f.rebind(config, {"a", "b"});
    REQUIRE(rebound);
    f.proc.set_snapshot(rebound);
    f.answer("a.example");
    REQUIRE(f.writer.recorded.size() == 1);
    CHECK(f.writer.recorded[0].set == "kpbr4d_a");
    f.answer("b.example");
    REQUIRE(f.writer.recorded.size() == 2);
    CHECK(f.writer.recorded[1].set == "kpbr4d_b");
}

TEST_CASE("intercept rebind: a list removed by the new config is not written") {
    RebindFixture f;
    f.proc.set_snapshot(build_intercept_snapshot(
        RebindFixture::config_with(kTwoLists, R"(["a","b"])"),
        RebindFixture::sets_for({"a", "b"}), false, f.effective, f.streamer));
    const Config next = RebindFixture::config_with(kTwoLists, R"(["a"])");  // b no longer routed
    const auto rebound = f.rebind(next, {"a"});
    REQUIRE(rebound);
    f.proc.set_snapshot(rebound);
    f.answer("b.example");
    CHECK(f.writer.recorded.empty());
    f.answer("a.example");  // the other list keeps learning
    REQUIRE(f.writer.recorded.size() == 1);
    CHECK(f.writer.recorded[0].set == "kpbr4d_a");
}

TEST_CASE("intercept rebind: a redefined list waits for the new index") {
    RebindFixture f;
    f.proc.set_snapshot(build_intercept_snapshot(
        RebindFixture::config_with(kTwoLists, R"(["a","b"])"),
        RebindFixture::sets_for({"a", "b"}), false, f.effective, f.streamer));
    // List b now has different content: the old index must not feed its set.
    const Config next = RebindFixture::config_with(
        R"({"a":{"domains":["a.example"]},"b":{"domains":["other.example"]}})", R"(["a","b"])");
    const auto rebound = f.rebind(next, {"a", "b"});
    REQUIRE(rebound);
    f.proc.set_snapshot(rebound);
    f.answer("b.example");
    CHECK(f.writer.recorded.empty());
    f.answer("a.example");
    CHECK(f.writer.recorded.size() == 1);
}

TEST_CASE("intercept rebind: a failed background build keeps the published snapshot") {
    RebindFixture f;
    const Config config = RebindFixture::config_with(kTwoLists, R"(["a","b"])");
    CHECK_FALSE(f.rebind(config, {"a", "b"}));  // nothing published: nothing to rebind
    f.proc.set_snapshot(build_intercept_snapshot(config, RebindFixture::sets_for({"a", "b"}), false,
                                                 f.effective, f.streamer));
    const auto rebound = f.rebind(config, {"a", "b"});
    f.proc.set_snapshot(rebound);
    // The build then fails or cannot be queued: the daemon simply never calls
    // set_snapshot again, so the rebound snapshot stays in use.
    CHECK(f.proc.current_snapshot() == rebound);
    f.answer("a.example");
    REQUIRE(f.writer.recorded.size() == 1);
    CHECK(f.writer.recorded[0].set == "kpbr4d_a");
}
