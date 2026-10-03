#include <doctest/doctest.h>

#include "../src/netfilter/set_writer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cerrno>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netlink.h>
#include <thread>
#include <string>
#include <optional>
#include <sys/stat.h>
#include <utility>
#include <vector>

using namespace keen_pbr3::nfnl;

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "test_set_writer golden bytes assume a little-endian host"
#endif

namespace {

std::vector<uint8_t> to_vec(const MsgBuilder& b) {
    return std::vector<uint8_t>(b.data(), b.data() + b.size());
}

SetAdd v4_add(uint32_t timeout_s) {
    SetAdd a{"s", 4, {}, timeout_s};
    a.addr = {1, 2, 3, 4};
    return a;
}

SetAdd v6_add(uint32_t timeout_s, std::string_view set = "s6") {
    SetAdd a{set, 6, {}, timeout_s};
    a.addr = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    return a;
}

// Golden bytes below come from `strace -x` of the real tools (seq normalized
// to 1, pid 0):
//   ipset v7.24: ipset add s 1.2.3.4 timeout 30      (set "s", hash:net timeout 0)
//   nft 1.1.7:   nft add element inet t s '{ 1.2.3.4 timeout 30s }'
// Deliberate differences from the captured traffic: ipset sends protocol 7
// and a trailing CADT_LINENO attribute (we send IPSET_PROTOCOL_MIN = 6, no
// lineno); nft sends no NLM_F_ACK/NLM_F_EXCL (we need both) and a batch-local
// NFTA_SET_ELEM_LIST_SET_ID attribute (unused for existing sets).

} // namespace

namespace {

struct FakeTransport final : SetWriterTransport {
    struct Call {
        uint32_t first{};
        uint32_t last{};
        int timeout_ms{};
        std::vector<uint16_t> types;
        std::vector<uint16_t> flags;
    };
    struct Reply {
        std::vector<int> errors;
        int result{0};
        int sleep_ms{0};
        bool stale_ack_before{false};
    };

    uint32_t next_seq() override { return ++seq; }

    int transact(const uint8_t* request, std::size_t length, uint32_t first_seq,
                 uint32_t last_seq, int timeout_ms,
                 const std::function<void(const MsgView&)>&,
                 const std::function<void(uint32_t, int)>& on_ack) override {
        Call call;
        call.first = first_seq;
        call.last = last_seq;
        call.timeout_ms = timeout_ms;
        REQUIRE(for_each_msg(keen_pbr3::ByteView(request, length), [&](const MsgView& msg) {
            call.types.push_back(msg.type);
            call.flags.push_back(msg.flags);
            return true;
        }));
        calls.push_back(call);
        Reply reply;
        if (!replies.empty()) {
            reply = std::move(replies.front());
            replies.erase(replies.begin());
        }
        if (reply.sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(reply.sleep_ms));
        }
        if (reply.stale_ack_before && first_seq != 0) {
            on_ack(first_seq - 1, EEXIST);
        }
        for (std::size_t i = 0; i < reply.errors.size(); ++i) {
            on_ack(first_seq + static_cast<uint32_t>(i), reply.errors[i]);
        }
        return reply.result;
    }

    uint32_t seq{0};
    std::vector<Call> calls;
    std::vector<Reply> replies;
};

SetAdd fake_v4(std::string_view set, uint8_t last) {
    SetAdd value{set, 4, {}, 30};
    value.addr = {192, 0, 2, last};
    return value;
}

} // namespace

TEST_CASE("set_writer: injected ipset mixed existing and new") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->replies = {
        {{EEXIST, 0}, 0, 0},
        {{0}, 0, 0},
    };
    auto writer = make_ipset_writer_for_test(std::move(transport));
    const SetAdd adds[] = {fake_v4("set", 1), fake_v4("set", 2)};
    SetAddResult results[2]{};

    REQUIRE(writer->add(adds, results, 2, 100));
    CHECK(results[0] == SetAddResult::Refreshed);
    CHECK(results[1] == SetAddResult::Added);
    REQUIRE(fake->calls.size() == 2);
    CHECK(fake->calls[0].types.size() == 2);
    CHECK(fake->calls[1].types.size() == 1);
}

TEST_CASE("set_writer: injected nft refresh is delete plus add") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    // First nft batch has one existing element and one new element.  The
    // kernel aborts the batch, so the successful new ACK is retried too.
    fake->replies = {
        {{EEXIST, 0}, EEXIST, 0},
        {{0, 0}, 0, 0},
        {{0, 0}, 0, 0},
    };
    auto writer = make_nft_writer_for_test("table", std::move(transport));
    const SetAdd adds[] = {fake_v4("set", 1), fake_v4("set", 2)};
    SetAddResult results[2]{};

    REQUIRE(writer->add(adds, results, 2, 100));
    CHECK(results[0] == SetAddResult::Refreshed);
    CHECK(results[1] == SetAddResult::Added);
    REQUIRE(fake->calls.size() == 3);
    CHECK(fake->calls[2].types.size() == 4); // batch begin, DEL, NEW, batch end
    CHECK(fake->calls[2].types[1] ==
          ((NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_DELSETELEM));
    CHECK(fake->calls[2].types[2] ==
          ((NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWSETELEM));
}

TEST_CASE("set_writer: nft partial batch ACK never reports unacknowledged Added") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    // Only the existing element was acknowledged before the atomic batch
    // failed.  The new element must remain Error, never Added.
    fake->replies = {
        {{EEXIST}, EEXIST, 0},
        {{0}, 0, 0},
        {{0, 0}, 0, 0},
    };
    auto writer = make_nft_writer_for_test("table", std::move(transport));
    const SetAdd adds[] = {fake_v4("set", 1), fake_v4("set", 2)};
    SetAddResult results[2]{};

    CHECK_FALSE(writer->add(adds, results, 2, 100));
    CHECK(results[0] == SetAddResult::Refreshed);
    CHECK(results[1] == SetAddResult::Error);
}

TEST_CASE("set_writer: nft ACK after batch error is retried, not accepted as Added") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    // The first element was ACKed before the second failed.  The retry also
    // fails, so the first element must not be reported as an Added success.
    fake->replies = {
        {{0, ENOENT}, ENOENT, 0},
        {{}, ETIMEDOUT, 0},
    };
    auto writer = make_nft_writer_for_test("table", std::move(transport));
    const SetAdd adds[] = {fake_v4("set", 1), fake_v4("set", 2)};
    SetAddResult results[2]{};

    CHECK_FALSE(writer->add(adds, results, 2, 100));
    CHECK(results[0] == SetAddResult::Error);
    CHECK(results[1] == SetAddResult::Error);
}

TEST_CASE("set_writer: nft refresh retries an expiration race once") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->replies = {
        {{EEXIST}, EEXIST, 0}, // exclusive probe
        {{0}, 0, 0},           // non-exclusive probe
        {{ENOENT, 0}, ENOENT, 0}, // DEL races with expiry; batch rolls back
        {{0}, 0, 0},           // bounded exclusive recreation succeeds
    };
    auto writer = make_nft_writer_for_test("table", std::move(transport));
    const SetAdd add = fake_v4("set", 3);
    SetAddResult result = SetAddResult::Error;

    REQUIRE(writer->add(&add, &result, 1, 100));
    CHECK(result == SetAddResult::Added);
    CHECK(fake->calls.size() == 4);
}

TEST_CASE("set_writer: second pass transport failure never reports refresh") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->replies = {
        {{EEXIST}, EEXIST, 0},
        {{0}, ETIMEDOUT, 0},
    };
    auto writer = make_ipset_writer_for_test(std::move(transport));
    const SetAdd add = fake_v4("set", 4);
    SetAddResult result = SetAddResult::Added;

    CHECK_FALSE(writer->add(&add, &result, 1, 100));
    CHECK(result == SetAddResult::Error);
    CHECK(writer->last_errno() == ETIMEDOUT);
}

TEST_CASE("set_writer: stale ACK before the batch cannot poison its result") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->replies = {{{0}, 0, 0, true}};
    auto writer = make_ipset_writer_for_test(std::move(transport));
    const SetAdd add = fake_v4("set", 5);
    SetAddResult result = SetAddResult::Error;

    REQUIRE(writer->add(&add, &result, 1, 100));
    CHECK(result == SetAddResult::Added);
}

TEST_CASE("set_writer: deadline stops before the next datagram") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->replies = {{{}, ETIMEDOUT, 10}};
    auto writer = make_ipset_writer_for_test(std::move(transport));
    std::vector<SetAdd> adds;
    for (int i = 0; i < 129; ++i) adds.push_back(fake_v4("set", static_cast<uint8_t>(i)));
    std::vector<SetAddResult> results(adds.size());

    CHECK_FALSE(writer->add(adds.data(), results.data(), adds.size(), 1));
    CHECK(fake->calls.size() == 1);
    CHECK(std::all_of(results.begin(), results.end(),
                      [](SetAddResult result) { return result == SetAddResult::Error; }));
}

TEST_CASE("set_writer: zero deadline performs no transport operation") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    auto writer = make_ipset_writer_for_test(std::move(transport));
    const SetAdd add = fake_v4("set", 1);
    SetAddResult result = SetAddResult::Added;

    CHECK_FALSE(writer->add(&add, &result, 1, 0));
    CHECK(result == SetAddResult::Error);
    CHECK(fake->calls.empty());
}

TEST_CASE("set_writer: ipset add v4 with timeout, exclusive") {
    MsgBuilder b;
    build_ipset_add(b, 1, v4_add(30), true);
    const std::vector<uint8_t> expected = {
        0x3c, 0x00, 0x00, 0x00, 0x09, 0x06, 0x05, 0x02,  // len 60, IPSET<<8|ADD, REQ|ACK|EXCL
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // seq 1, pid 0
        0x02, 0x00, 0x00, 0x00,                          // AF_INET, v0, res_id 0
        0x05, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00, 0x00,  // PROTOCOL = 6
        0x06, 0x00, 0x02, 0x00, 's',  0x00, 0x00, 0x00,  // SETNAME "s"
        0x18, 0x00, 0x07, 0x80,                          // DATA | NESTED
        0x0c, 0x00, 0x01, 0x80,                          // IP | NESTED
        0x08, 0x00, 0x01, 0x40, 0x01, 0x02, 0x03, 0x04,  // IPADDR_IPV4 | NET_BYTEORDER
        0x08, 0x00, 0x06, 0x40, 0x00, 0x00, 0x00, 0x1e,  // TIMEOUT 30 (BE)
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("set_writer: ipset add v4 non-exclusive differs only in flags") {
    MsgBuilder excl, plain;
    build_ipset_add(excl, 1, v4_add(30), true);
    build_ipset_add(plain, 1, v4_add(30), false);
    auto a = to_vec(excl);
    auto p = to_vec(plain);
    REQUIRE(a.size() == p.size());
    CHECK(p[6] == 0x05);
    CHECK(p[7] == 0x00);
    a[7] = 0x00;
    CHECK(a == p);
}

TEST_CASE("set_writer: ipset add v6 permanent-timeout field present") {
    MsgBuilder b;
    build_ipset_add(b, 1, v6_add(30), false);
    const std::vector<uint8_t> expected = {
        0x48, 0x00, 0x00, 0x00, 0x09, 0x06, 0x05, 0x00,  // len 72, REQ|ACK
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00,                          // AF_INET as sent by ipset
        0x05, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x02, 0x00, 's',  '6',  0x00, 0x00,  // SETNAME "s6"
        0x24, 0x00, 0x07, 0x80,
        0x18, 0x00, 0x01, 0x80,
        0x14, 0x00, 0x02, 0x40,                          // IPADDR_IPV6 | NET_BYTEORDER
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x00, 0x06, 0x40, 0x00, 0x00, 0x00, 0x1e,
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("set_writer: ipset add timeout 0 is still sent") {
    MsgBuilder b;
    build_ipset_add(b, 7, v4_add(0), true);
    const auto v = to_vec(b);
    REQUIRE(v.size() == 60);
    CHECK(v[8] == 0x07);  // seq
    const std::vector<uint8_t> tail = {0x08, 0x00, 0x06, 0x40, 0x00, 0x00, 0x00, 0x00};
    CHECK(std::vector<uint8_t>(v.end() - 8, v.end()) == tail);
}

TEST_CASE("set_writer: nft newsetelem v4 with timeout 30s, exclusive") {
    MsgBuilder b;
    build_nft_newsetelem(b, 1, "t", v4_add(30), true);
    const std::vector<uint8_t> expected = {
        0x44, 0x00, 0x00, 0x00, 0x0c, 0x0a, 0x05, 0x06,  // len 68, NFTABLES<<8|NEWSETELEM, REQ|CREATE|ACK|EXCL
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,                          // NFPROTO_INET, v0, res_id 0
        0x06, 0x00, 0x01, 0x00, 't',  0x00, 0x00, 0x00,  // LIST_TABLE "t"
        0x06, 0x00, 0x02, 0x00, 's',  0x00, 0x00, 0x00,  // LIST_SET "s"
        0x20, 0x00, 0x03, 0x80,                          // LIST_ELEMENTS | NESTED
        0x1c, 0x00, 0x01, 0x80,                          // LIST_ELEM | NESTED
        0x0c, 0x00, 0x04, 0x00,                          // ELEM_TIMEOUT
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x30,  // 30000 ms (BE)
        0x0c, 0x00, 0x01, 0x80,                          // ELEM_KEY | NESTED
        0x08, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,  // DATA_VALUE
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("set_writer: nft newsetelem v6 without timeout, non-exclusive") {
    MsgBuilder b;
    build_nft_newsetelem(b, 1, "t", v6_add(0), false);
    const std::vector<uint8_t> expected = {
        0x44, 0x00, 0x00, 0x00, 0x0c, 0x0a, 0x05, 0x04,  // REQ|CREATE|ACK
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x06, 0x00, 0x01, 0x00, 't',  0x00, 0x00, 0x00,
        0x07, 0x00, 0x02, 0x00, 's',  '6',  0x00, 0x00,  // "s6"
        0x20, 0x00, 0x03, 0x80,
        0x1c, 0x00, 0x01, 0x80,
        0x18, 0x00, 0x01, 0x80,
        0x14, 0x00, 0x01, 0x00,
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    };
    CHECK(to_vec(b) == expected);
}

namespace {

std::string run_capture(const std::string& cmd) {
    std::string out;
    FILE* f = ::popen((cmd + " 2>&1").c_str(), "r");
    if (f == nullptr) return out;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    ::pclose(f);
    return out;
}

int sh(const std::string& cmd) { return std::system(cmd.c_str()); }

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

bool isolated_netns_guard() {
    const char* enabled = std::getenv("KPBR_NETLINK_IT");
    const char* guard = std::getenv("KPBR_NETLINK_IN_NETNS");
    const char* parent_id = std::getenv("KPBR_NETLINK_PARENT_NS");
    if (enabled == nullptr || std::string(enabled) != "1" || guard == nullptr ||
        std::string(guard) != "1" || parent_id == nullptr ||
        std::string(parent_id).empty()) {
        return false;
    }
    struct stat self{};
    if (::stat("/proc/self/ns/net", &self) != 0) {
        return false;
    }
    const std::string current = std::to_string(self.st_dev) + ":" +
                                std::to_string(self.st_ino);
    return current != parent_id;
}

std::optional<unsigned> nft_expires_seconds(const std::string& text) {
    const std::string needle = "expires ";
    const auto at = text.find(needle);
    if (at == std::string::npos) return std::nullopt;
    const auto begin = at + needle.size();
    std::size_t end = begin;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
    if (end == begin) return std::nullopt;
    try {
        return static_cast<unsigned>(std::stoul(text.substr(begin, end - begin)));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<SetAdd> bulk_v4(std::string_view set, int first, int count, uint32_t timeout) {
    std::vector<SetAdd> v;
    for (int i = 0; i < count; ++i) {
        SetAdd a{set, 4, {}, timeout};
        const int n = first + i;
        a.addr = {10, static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 8),
                  static_cast<uint8_t>(n)};
        v.push_back(a);
    }
    return v;
}

double time_batches(DynamicSetWriter& w, std::string_view set, int total, int batch) {
    const auto all = bulk_v4(set, 1, total, 600);
    std::vector<SetAddResult> res(static_cast<std::size_t>(batch));
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < total; i += batch) {
        const int n = std::min(batch, total - i);
        REQUIRE(w.add(all.data() + i, res.data(), static_cast<std::size_t>(n), 2000));
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

} // namespace

TEST_CASE("set_writer: live ipset/nft (isolated netns only)") {
    // KPBR_NETLINK_IT alone is deliberately insufficient: these operations
    // mutate kernel sets.  The second guard plus namespace comparison makes
    // accidental host-network execution fail closed.
    if (!isolated_netns_guard()) return;

    const std::string table = "KpbrWriterTest";

    SUBCASE("ipset") {
        REQUIRE(sh("ipset create kpbr4d_wt hash:net family inet timeout 0 -exist") == 0);
        REQUIRE(sh("ipset create kpbr6d_wt hash:net family inet6 timeout 0 -exist") == 0);
        auto w = make_ipset_writer();

        SetAdd adds[3] = {v4_add(0), v4_add(0), v6_add(0)};
        adds[0].set_name = "kpbr4d_wt";
        adds[0].timeout_s = 3600;
        adds[1] = adds[0];
        adds[1].addr = {5, 6, 7, 8};
        adds[1].timeout_s = 0;  // permanent
        adds[2].set_name = "kpbr6d_wt";
        adds[2].timeout_s = 3600;
        SetAddResult res[3];

        CHECK(w->add(adds, res, 3, 2000));
        for (auto r : res) CHECK(r == SetAddResult::Added);
        CHECK(w->add(adds, res, 3, 2000));
        for (auto r : res) CHECK(r == SetAddResult::Refreshed);

        // Mixed batch: one new + one existing.
        SetAdd mixed[2] = {adds[0], adds[0]};
        mixed[1].addr = {9, 9, 9, 9};
        SetAddResult mres[2];
        CHECK(w->add(mixed, mres, 2, 2000));
        CHECK(mres[0] == SetAddResult::Refreshed);
        CHECK(mres[1] == SetAddResult::Added);

        // Unknown set -> Error.
        SetAdd bad = adds[0];
        bad.set_name = "kpbr4d_nope";
        SetAddResult bres;
        CHECK_FALSE(w->add(&bad, &bres, 1, 2000));
        CHECK(bres == SetAddResult::Error);
        CHECK(w->last_errno() != 0);

        const std::string l4 = run_capture("ipset list kpbr4d_wt");
        const std::string l6 = run_capture("ipset list kpbr6d_wt");
        CHECK(contains(l4, "1.2.3.4 timeout 3"));
        CHECK(contains(l4, "5.6.7.8 timeout 0"));
        CHECK(contains(l4, "9.9.9.9 timeout 3"));
        CHECK(contains(l6, "2001:db8::1 timeout 3"));

        run_capture("ipset flush kpbr4d_wt");
        const double ms = time_batches(*w, "kpbr4d_wt", 1000, 8);
        MESSAGE("ipset: 1000 v4 elements in batches of 8: " << ms << " ms");
        CHECK(contains(run_capture("ipset list kpbr4d_wt -t"), "Number of entries: 1000"));

        sh("ipset destroy kpbr4d_wt");
        sh("ipset destroy kpbr6d_wt");
    }

    SUBCASE("nft") {
        REQUIRE(sh("nft add table inet " + table) == 0);
        REQUIRE(sh("nft add set inet " + table +
                   " kpbr4d_wt '{ type ipv4_addr; flags timeout; }'") == 0);
        REQUIRE(sh("nft add set inet " + table +
                   " kpbr6d_wt '{ type ipv6_addr; flags timeout; }'") == 0);
        auto w = make_nft_writer(table);

        // Prove that refresh changes the kernel expiry rather than merely
        // receiving a successful no-op NEWSETELEM.  The first short lease is
        // refreshed to a materially longer lease while it is still alive.
        SetAdd ttl = v4_add(2);
        ttl.set_name = "kpbr4d_wt";
        ttl.addr = {11, 22, 33, 44};
        SetAddResult ttl_result;
        REQUIRE(w->add(&ttl, &ttl_result, 1, 2000));
        REQUIRE(ttl_result == SetAddResult::Added);
        const auto before = nft_expires_seconds(
            run_capture("nft list set inet " + table + " kpbr4d_wt"));
        REQUIRE(before.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ttl.timeout_s = 30;
        REQUIRE(w->add(&ttl, &ttl_result, 1, 2000));
        REQUIRE(ttl_result == SetAddResult::Refreshed);
        const auto after = nft_expires_seconds(
            run_capture("nft list set inet " + table + " kpbr4d_wt"));
        REQUIRE(after.has_value());
        CHECK(*after > *before);
        CHECK(*after >= 20U);

        SetAdd adds[3] = {v4_add(0), v4_add(0), v6_add(0)};
        adds[0].set_name = "kpbr4d_wt";
        adds[0].timeout_s = 3600;
        adds[1] = adds[0];
        adds[1].addr = {5, 6, 7, 8};
        adds[1].timeout_s = 0;
        adds[2].set_name = "kpbr6d_wt";
        adds[2].timeout_s = 3600;
        SetAddResult res[3];

        CHECK(w->add(adds, res, 3, 2000));
        for (auto r : res) CHECK(r == SetAddResult::Added);
        CHECK(w->add(adds, res, 3, 2000));
        for (auto r : res) CHECK(r == SetAddResult::Refreshed);

        SetAdd mixed[2] = {adds[0], adds[0]};
        mixed[1].addr = {9, 9, 9, 9};
        SetAddResult mres[2];
        CHECK(w->add(mixed, mres, 2, 2000));
        CHECK(mres[0] == SetAddResult::Refreshed);
        CHECK(mres[1] == SetAddResult::Added);

        SetAdd bad = adds[0];
        bad.set_name = "kpbr4d_nope";
        SetAddResult bres;
        CHECK_FALSE(w->add(&bad, &bres, 1, 2000));
        CHECK(bres == SetAddResult::Error);
        CHECK(w->last_errno() != 0);

        const std::string l4 = run_capture("nft list set inet " + table + " kpbr4d_wt");
        const std::string l6 = run_capture("nft list set inet " + table + " kpbr6d_wt");
        CHECK(contains(l4, "1.2.3.4 timeout 1h"));
        CHECK(contains(l4, "9.9.9.9 timeout 1h"));
        CHECK(contains(l4, "5.6.7.8"));
        CHECK(contains(l6, "2001:db8::1 timeout 1h"));

        run_capture("nft flush set inet " + table + " kpbr4d_wt");
        const double ms = time_batches(*w, "kpbr4d_wt", 1000, 8);
        MESSAGE("nft: 1000 v4 elements in batches of 8: " << ms << " ms");
        CHECK(contains(run_capture("nft list set inet " + table + " kpbr4d_wt"), "10.0.3.232"));

        sh("nft delete table inet " + table);
    }
}
