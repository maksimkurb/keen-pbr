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
