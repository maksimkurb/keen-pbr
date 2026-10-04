#include <doctest/doctest.h>

#include "../src/intercept/set_element_cache.hpp"

using namespace keen_pbr3;

namespace {

using State = SetElementCache::State;

std::array<uint8_t, 16> v4(uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t d = 1) {
    std::array<uint8_t, 16> out{};
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = d;
    return out;
}

std::array<uint8_t, 16> v6(uint8_t last) {
    std::array<uint8_t, 16> out{};
    out[0] = 0x20;
    out[1] = 0x01;
    out[15] = last;
    return out;
}

} // namespace

TEST_CASE("set cache: record then lookup is Fresh until the expiry margin") {
    SetElementCache cache;
    const uint64_t epoch = cache.epoch();
    CHECK(cache.lookup(3, 4, v4(1), 1000).state == State::Unknown);
    REQUIRE(cache.record(3, 4, v4(1), 300, 1000, epoch));
    const auto hit = cache.lookup(3, 4, v4(1), 2000);
    CHECK(hit.state == State::Fresh);
    CHECK(hit.expires_at_ms == 1000 + 300000);
    CHECK(hit.written_at_ms == 1000);
    // Just outside / inside the 2 s margin before expiry (301000 - 2000).
    CHECK(cache.lookup(3, 4, v4(1), 298999).state == State::Fresh);
    CHECK(cache.lookup(3, 4, v4(1), 299000).state == State::Unknown);
    CHECK(cache.lookup(3, 4, v4(1), 400000).state == State::Unknown);
    CHECK(cache.size() == 1);
}

TEST_CASE("set cache: timeout 0 is permanent and stale only by trust age") {
    SetElementCache cache;
    REQUIRE(cache.record(0, 4, v4(9), 0, 5000, cache.epoch()));
    const auto hit = cache.lookup(0, 4, v4(9), 6000);
    CHECK(hit.state == State::Fresh);
    CHECK(hit.expires_at_ms == SetElementCache::kPermanent);
    CHECK(cache.lookup(0, 4, v4(9), 5000 + SetElementCache::kMaxTrustMs).state == State::Fresh);
    CHECK(cache.lookup(0, 4, v4(9), 5001 + SetElementCache::kMaxTrustMs).state == State::Stale);
}

TEST_CASE("set cache: a long timeout past the trust age is Stale, not Unknown") {
    SetElementCache cache;
    REQUIRE(cache.record(1, 4, v4(2), 86400, 0, cache.epoch()));
    CHECK(cache.lookup(1, 4, v4(2), SetElementCache::kMaxTrustMs).state == State::Fresh);
    const auto stale = cache.lookup(1, 4, v4(2), SetElementCache::kMaxTrustMs + 1);
    CHECK(stale.state == State::Stale);
    CHECK(stale.written_at_ms == 0);
}

TEST_CASE("set cache: re-recording updates the expiry in place") {
    SetElementCache cache;
    REQUIRE(cache.record(1, 4, v4(2), 10, 0, cache.epoch()));
    REQUIRE(cache.record(1, 4, v4(2), 100, 5000, cache.epoch()));
    CHECK(cache.size() == 1);
    CHECK(cache.lookup(1, 4, v4(2), 6000).expires_at_ms == 105000);
}

TEST_CASE("set cache: slots and families are isolated") {
    SetElementCache cache;
    const uint64_t epoch = cache.epoch();
    REQUIRE(cache.record(1, 4, v4(5), 300, 0, epoch));
    CHECK(cache.lookup(2, 4, v4(5), 1).state == State::Unknown);
    CHECK(cache.lookup(1, 6, v4(5), 1).state == State::Unknown);
    // v6 addresses with the same first 4 bytes differ in the rest.
    REQUIRE(cache.record(1, 6, v6(1), 300, 0, epoch));
    CHECK(cache.lookup(1, 6, v6(1), 1).state == State::Fresh);
    CHECK(cache.lookup(1, 6, v6(2), 1).state == State::Unknown);
    CHECK(cache.lookup(1, 4, v4(5), 1).state == State::Fresh);
    // For v4 only the first four bytes are significant.
    auto padded = v4(5);
    padded[10] = 0x77;
    CHECK(cache.lookup(1, 4, padded, 1).state == State::Fresh);
    CHECK(cache.lookup(SetElementCache::kNoSlot, 4, v4(5), 1).state == State::Unknown);
    CHECK_FALSE(cache.record(SetElementCache::kNoSlot, 4, v4(5), 300, 0, epoch));
}

TEST_CASE("set cache: clear forgets everything and rejects records from an older epoch") {
    SetElementCache cache;
    const uint64_t old_epoch = cache.epoch();
    REQUIRE(cache.record(1, 4, v4(5), 300, 0, old_epoch));
    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(cache.epoch() != old_epoch);
    CHECK(cache.lookup(1, 4, v4(5), 1).state == State::Unknown);
    // A write that started before the clear must not repopulate the cache.
    CHECK_FALSE(cache.record(1, 4, v4(5), 300, 0, old_epoch));
    CHECK(cache.lookup(1, 4, v4(5), 1).state == State::Unknown);
    CHECK(cache.record(1, 4, v4(5), 300, 0, cache.epoch()));
}

TEST_CASE("set cache: a full probe window evicts the entry that expires first") {
    SetElementCache cache;
    const uint64_t epoch = cache.epoch();
    // Insert far more entries than the capacity: the table never grows and
    // never fails, and the newest entries stay findable.
    const std::size_t total = SetElementCache::kSetCacheCapacity * 2;
    for (std::size_t i = 0; i < total; ++i) {
        const auto addr = v4(10, static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8),
                             static_cast<uint8_t>(i));
        // Later entries expire later, so the earliest ones are evicted first.
        REQUIRE(cache.record(1, 4, addr, 1000 + static_cast<uint32_t>(i % 5000), 0, epoch));
    }
    CHECK(cache.size() <= SetElementCache::kSetCacheCapacity);
    std::size_t found = 0;
    for (std::size_t i = total - 1000; i < total; ++i) {
        const auto addr = v4(10, static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8),
                             static_cast<uint8_t>(i));
        if (cache.lookup(1, 4, addr, 1).state == State::Fresh) ++found;
    }
    CHECK(found > 500);
}

TEST_CASE("set cache: eviction picks the smallest expiry inside the window") {
    // Fill one probe window by brute force: keep inserting entries that share
    // nothing but the table until an insert has to evict, then check that a
    // long-lived entry survives while a short-lived one is the victim.
    SetElementCache cache;
    const uint64_t epoch = cache.epoch();
    REQUIRE(cache.record(7, 4, v4(100, 0, 0, 1), 100000, 0, epoch));   // long lived
    REQUIRE(cache.record(7, 4, v4(100, 0, 0, 2), 3, 0, epoch));         // short lived
    for (std::size_t i = 0; i < SetElementCache::kSetCacheCapacity * 4; ++i) {
        const auto addr = v4(11, static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8),
                             static_cast<uint8_t>(i));
        cache.record(8, 4, addr, 50000, 0, epoch);
    }
    CHECK(cache.lookup(7, 4, v4(100, 0, 0, 1), 1).state == State::Fresh);
    CHECK(cache.lookup(7, 4, v4(100, 0, 0, 2), 1).state == State::Unknown);
}

TEST_CASE("needs_refresh: permanent timeout over permanent cache") {
    // Both permanent: no refresh needed.
    CHECK_FALSE(SetElementCache::needs_refresh(1000, 0, true, true));
}

TEST_CASE("needs_refresh: finite timeout over permanent cache") {
    // Cached is permanent (infinite expiry), desired is finite: no refresh.
    CHECK_FALSE(SetElementCache::needs_refresh(50000, 100, true, false));
}

TEST_CASE("needs_refresh: permanent timeout over finite cache") {
    // Cached is finite, desired is permanent (timeout 0): always refresh.
    CHECK(SetElementCache::needs_refresh(50000, 0, false, true));
}

TEST_CASE("needs_refresh: finite timeout with remaining at various thresholds") {
    // Both finite. Test remaining at boundaries of half-timeout.
    const uint32_t timeout_s = 100;
    const int64_t half_timeout_ms = 50000;  // 100 * 500

    // remaining < half: refresh needed
    CHECK(SetElementCache::needs_refresh(0, timeout_s, false, false));
    CHECK(SetElementCache::needs_refresh(49999, timeout_s, false, false));
    // remaining == half: no refresh (boundary: we want <, not <=)
    CHECK_FALSE(SetElementCache::needs_refresh(50000, timeout_s, false, false));
    // remaining > half: no refresh
    CHECK_FALSE(SetElementCache::needs_refresh(50001, timeout_s, false, false));
    CHECK_FALSE(SetElementCache::needs_refresh(100000, timeout_s, false, false));
}

TEST_CASE("needs_refresh: zero timeout (permanent) over finite cache") {
    // Covers the special case: timeout_s==0 with finite cache.
    CHECK(SetElementCache::needs_refresh(10000, 0, false, true));
}
