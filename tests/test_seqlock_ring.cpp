#include <doctest/doctest.h>

#include "../src/intercept/seqlock_ring.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

// Every word of `fill` is derived from `key`, so a torn copy (words of two
// different writes) is detectable.
struct Rec {
    uint64_t seq{0};
    uint64_t key{0};
    uint64_t fill[14]{};
};

void stamp_rec(Rec& r, uint64_t key) {
    r.key = key;
    for (std::size_t i = 0; i < 14; ++i) r.fill[i] = key * 31 + i;
}

bool consistent(const Rec& r) {
    for (std::size_t i = 0; i < 14; ++i) {
        if (r.fill[i] != r.key * 31 + i) return false;
    }
    return true;
}

using Ring = SeqlockRing<Rec, 64>;

} // namespace

TEST_CASE("seqlock ring: push, read, bounded and ordered") {
    Ring ring;
    CHECK(ring.last_seq() == 0);
    for (uint64_t i = 1; i <= 100; ++i) {
        Rec r;
        stamp_rec(r, i);
        CHECK(ring.push(r) == i);
    }
    CHECK(ring.last_seq() == 100);
    std::vector<Rec> out;
    ring.read(0, 1000, out);
    REQUIRE(out.size() == 64);
    CHECK(out.front().seq == 37);
    CHECK(out.back().seq == 100);
    for (const Rec& r : out) CHECK(consistent(r));
    out.clear();
    ring.read(95, 1000, out);
    CHECK(out.size() == 5);
    out.clear();
    ring.read(0, 3, out);
    CHECK(out.size() == 3);
    out.clear();
    ring.read(100, 10, out);
    CHECK(out.empty());
}

TEST_CASE("seqlock ring: in-place update and lost records") {
    Ring ring;
    for (uint64_t i = 1; i <= 100; ++i) {
        Rec r;
        stamp_rec(r, i);
        ring.push(r);
    }
    CHECK_FALSE(ring.update(10, [](Rec&) { return true; }));  // overwritten
    CHECK(ring.update(100, [](Rec& r) {
        stamp_rec(r, 7777);
        return true;
    }));
    Rec r;
    CHECK(ring.peek(100, r) == Ring::Peek::Ok);
    CHECK(r.key == 7777);
    CHECK(consistent(r));
    CHECK(ring.peek(10, r) == Ring::Peek::Lost);
    CHECK(ring.peek(101, r) == Ring::Peek::Pending);
}

TEST_CASE("seqlock ring: concurrent producers and a reader never see a torn record") {
    Ring ring;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> torn{0};
    std::atomic<uint64_t> read_total{0};
    std::thread reader([&] {
        uint64_t after = 0;
        std::vector<Rec> out;
        while (!stop.load()) {
            out.clear();
            ring.read(after, 64, out);
            uint64_t prev = after;
            for (const Rec& r : out) {
                if (!consistent(r) || r.seq <= prev) torn.fetch_add(1);
                prev = r.seq;
            }
            if (!out.empty()) after = out.back().seq;
            read_total.fetch_add(out.size());
        }
    });
    constexpr uint64_t kPerProducer = 100000;
    std::vector<std::thread> producers;
    for (int p = 0; p < 2; ++p) {
        producers.emplace_back([&, p] {
            for (uint64_t i = 0; i < kPerProducer; ++i) {
                Rec r;
                stamp_rec(r, i * 2 + static_cast<uint64_t>(p));
                ring.push(r);
                if (i % 8 == 0) {  // in-place rewrites race with the reader too
                    ring.update(ring.last_seq(), [](Rec& rec) {
                        stamp_rec(rec, rec.key);
                        return true;
                    });
                }
            }
        });
    }
    for (auto& t : producers) t.join();
    stop.store(true);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(ring.last_seq() == 2 * kPerProducer);
    CHECK(read_total.load() > 0);
}

// MIPS32 routers have no lock-free 64-bit atomics: the ring then stores
// 32-bit words, and a record's 64-bit seq spans two of them.
TEST_CASE("seqlock ring: 32-bit words keep seq, updates and overwrites exact") {
    using Ring32 = SeqlockRing<Rec, 64, uint32_t>;
    Ring32 ring;
    const uint64_t big = (uint64_t{1} << 32) + 5;  // seq bits in both words
    for (uint64_t i = 1; i <= 100; ++i) {
        Rec r;
        stamp_rec(r, i + big);
        CHECK(ring.push(r) == i);
    }
    std::vector<Rec> out;
    ring.read(0, 1000, out);
    REQUIRE(out.size() == 64);
    CHECK(out.front().seq == 37);
    CHECK(out.back().seq == 100);
    for (const Rec& r : out) CHECK(consistent(r));
    CHECK(out.back().key == 100 + big);

    CHECK_FALSE(ring.update(10, [](Rec&) { return true; }));
    CHECK(ring.update(100, [&](Rec& r) {
        stamp_rec(r, big * 3);
        return true;
    }));
    Rec r;
    CHECK(ring.peek(100, r) == Ring32::Peek::Ok);
    CHECK(r.key == big * 3);
    CHECK(consistent(r));
    CHECK(ring.peek(10, r) == Ring32::Peek::Lost);
    CHECK(ring.peek(101, r) == Ring32::Peek::Pending);
}
