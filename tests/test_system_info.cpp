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

TEST_CASE("parse_keenetic_version_from_rci_response reads legacy release field") {
    const auto version = parse_keenetic_version_from_rci_response(R"({
        "release": "2.16.D.12.0-12",
        "device": "Keenetic Giga II"
    })");

    REQUIRE(version.has_value());
    CHECK(*version == "2.16.D.12.0-12");
    CHECK(parse_keenetic_major_version(*version) == std::optional<int>(2));
    CHECK_FALSE(keenetic_version_supports_encrypted_dns(*version));
}

TEST_CASE("parse_keenetic_version_from_rci_response accepts plain title payloads") {
    const auto version = parse_keenetic_version_from_rci_response("\"4.3.2\"");

    REQUIRE(version.has_value());
    CHECK(*version == "4.3.2");
    CHECK(parse_keenetic_major_version(*version) == std::optional<int>(4));
    CHECK(keenetic_version_supports_encrypted_dns(*version));
}

} // namespace keen_pbr3
