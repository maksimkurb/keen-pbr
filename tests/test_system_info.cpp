#include "../src/util/system_info.hpp"

#include <doctest/doctest.h>

namespace keen_pbr3 {

TEST_CASE("detect_system_info returns populated fields") {
    const SystemInfo info = detect_system_info();

    CHECK_FALSE(info.os_type.empty());
    CHECK_FALSE(info.os_version.empty());
    CHECK_FALSE(info.build_variant.empty());
}

TEST_CASE("cached_system_info returns stable snapshot") {
    const SystemInfo& first = cached_system_info();
    const SystemInfo& second = cached_system_info();

    CHECK(&first == &second);
}

} // namespace keen_pbr3
