#ifdef WITH_API

#include <doctest/doctest.h>

#include "../src/health/runtime_interface_inventory.hpp"

namespace keen_pbr3 {

TEST_CASE("build_runtime_interface_inventory_response: empty interfaces returns empty response") {
    const auto response = build_runtime_interface_inventory_response(
        std::vector<DumpedInterface>{});

    CHECK(response.interfaces.empty());
}

TEST_CASE("build_runtime_interface_inventory_response: admin-up interface maps to up") {
    DumpedInterface dumped;
    dumped.name = "br0";
    dumped.admin_up = true;
    dumped.oper_state = std::string("up");
    dumped.carrier = true;
    dumped.ipv4_addresses = {"192.168.1.1/24"};
    dumped.ipv6_addresses = {"fe80::1/64"};

    const auto response = build_runtime_interface_inventory_response(
        std::vector<DumpedInterface>{dumped});

    REQUIRE(response.interfaces.size() == 1);
    const auto& entry = response.interfaces.front();
    CHECK(entry.name == "br0");
    CHECK(entry.status == api::RuntimeInterfaceInventoryStatusEnum::UP);
    REQUIRE(entry.admin_up.has_value());
    CHECK(*entry.admin_up);
    REQUIRE(entry.oper_state.has_value());
    CHECK(*entry.oper_state == "up");
    REQUIRE(entry.carrier.has_value());
    CHECK(*entry.carrier);
    REQUIRE(entry.ipv4_addresses.has_value());
    CHECK(*entry.ipv4_addresses == std::vector<std::string>{"192.168.1.1/24"});
    REQUIRE(entry.ipv6_addresses.has_value());
    CHECK(*entry.ipv6_addresses == std::vector<std::string>{"fe80::1/64"});
}

TEST_CASE("build_runtime_interface_inventory_response: admin-down interface maps to down") {
    DumpedInterface dumped;
    dumped.name = "wg0";
    dumped.admin_up = false;

    const auto response = build_runtime_interface_inventory_response(
        std::vector<DumpedInterface>{dumped});

    REQUIRE(response.interfaces.size() == 1);
    const auto& entry = response.interfaces.front();
    CHECK(entry.name == "wg0");
    CHECK(entry.status == api::RuntimeInterfaceInventoryStatusEnum::DOWN);
    REQUIRE(entry.admin_up.has_value());
    CHECK_FALSE(*entry.admin_up);
    CHECK_FALSE(entry.oper_state.has_value());
    CHECK_FALSE(entry.carrier.has_value());
    CHECK_FALSE(entry.ipv4_addresses.has_value());
    CHECK_FALSE(entry.ipv6_addresses.has_value());
}

} // namespace keen_pbr3

#endif // WITH_API
