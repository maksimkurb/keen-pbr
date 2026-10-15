#include <doctest/doctest.h>

#include "../src/daemon/config_store.hpp"

using namespace keen_pbr3;

namespace {

Config config_named(const char* name) {
    Config config;
    config.device_name = name;
    return config;
}

} // namespace

TEST_CASE("config store atomically promotes the applied staged revision") {
    ConfigStore store(config_named("old"));
    const Config applied = config_named("new");

    store.stage_config(applied);
    const auto staged = store.staged_snapshot();
    REQUIRE(staged.has_value());

    store.replace_active_and_clear_staged_if_revision(
        applied, OutboundMarkMap{}, staged->revision);

    CHECK(store.active_config().device_name.value_or("") == "new");
    CHECK(store.visible_config().device_name.value_or("") == "new");
    CHECK_FALSE(store.config_is_draft());
}

TEST_CASE(
    "config store preserves a newer draft while promoting an older revision") {
    ConfigStore store(config_named("old"));
    const Config applied = config_named("applied");
    const Config newer = config_named("newer");

    store.stage_config(applied);
    const auto applying = store.staged_snapshot();
    REQUIRE(applying.has_value());

    store.stage_config(newer);
    store.replace_active_and_clear_staged_if_revision(
        applied, OutboundMarkMap{}, applying->revision);

    CHECK(store.active_config().device_name.value_or("") == "applied");
    CHECK(store.visible_config().device_name.value_or("") == "newer");
    CHECK(store.config_is_draft());
}

TEST_CASE("config store bumps its revision on every active and draft change") {
    ConfigStore store(config_named("old"));
    int notified = 0;
    store.set_change_callback([&] { ++notified; });

    auto last = store.revision();
    const auto bumped = [&] {
        const auto now = store.revision();
        const bool moved = now > last;
        last = now;
        return moved;
    };

    store.stage_config(config_named("draft"));
    CHECK(bumped());
    store.stage_config(config_named("draft2"));
    CHECK(bumped());

    store.clear_staged();
    CHECK(bumped());
    // Nothing to discard: no change, no signal.
    store.clear_staged();
    CHECK_FALSE(bumped());

    store.stage_config(config_named("applied"));
    CHECK(bumped());
    const auto staged = store.staged_snapshot();
    REQUIRE(staged.has_value());
    store.replace_active_and_clear_staged_if_revision(
        config_named("applied"), OutboundMarkMap{}, staged->revision);
    CHECK(bumped());

    store.replace_active(config_named("rolled-back"), OutboundMarkMap{});
    CHECK(bumped());

    // A stale revision does not clear (or signal for) a newer draft.
    store.stage_config(config_named("newer"));
    CHECK(bumped());
    store.clear_staged_if_revision(staged->revision);
    CHECK_FALSE(bumped());
    CHECK(store.config_is_draft());

    // External state shown next to the config (list refresh state).
    store.touch();
    CHECK(bumped());

    // The callback ran once per bump.
    CHECK(notified == 8);
}

TEST_CASE("config store callback runs without the store lock held") {
    ConfigStore store(config_named("old"));
    bool reentered = false;
    store.set_change_callback([&] {
        (void)store.revision();
        (void)store.visible_config();
        reentered = true;
    });
    store.stage_config(config_named("draft"));
    CHECK(reentered);
}
