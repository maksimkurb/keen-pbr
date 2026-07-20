#ifdef WITH_API

#include <doctest/doctest.h>

#include "../src/keenetic/interface_descriptions.hpp"
#include "../src/util/system_info.hpp"

namespace keen_pbr3 {
namespace {

api::RuntimeInterfaceInventoryResponse inventory(
    std::string name, std::vector<std::string> ipv4_addresses = {}) {
    api::RuntimeInterfaceInventoryEntry entry;
    entry.name = std::move(name);
    entry.status = api::RuntimeInterfaceInventoryStatusEnum::UP;
    if (!ipv4_addresses.empty()) entry.ipv4_addresses = std::move(ipv4_addresses);
    return {{entry}};
}

struct KeeneticInterfaceTestState {
    KeeneticInterfaceTestState() { reset_keenetic_interface_test_state(); }
    ~KeeneticInterfaceTestState() {
        reset_keenetic_interface_test_state();
        reset_system_info_for_tests();
    }
};

} // namespace

TEST_CASE("Keenetic descriptions: modern RCI maps parent interface descriptions") {
    KeeneticInterfaceTestState state;
    set_system_info_for_tests(SystemInfo{"keenetic", "4.03.C.1", "keenetic"});
    int calls = 0;
    set_keenetic_interface_fetcher_for_tests([&calls](const std::string& method,
                                                       const std::string&,
                                                       const std::string&) {
        ++calls;
        if (method == "GET") {
            return R"({"GigabitEthernet1":{"id":"GigabitEthernet1","description":"Internet","connected":"yes"},"GigabitEthernet1/0":{"id":"GigabitEthernet1/0","description":"Child"}})";
        }
        return R"({"show":{"interface":[{"system-name":"eth3"},{"system-name":"eth3"}]}})";
    });

    auto response = inventory("eth3");
    populate_keenetic_interface_descriptions(response);

    REQUIRE(response.interfaces.front().description.has_value());
    CHECK(*response.interfaces.front().description == "Internet");
    CHECK(calls == 2);
}

TEST_CASE("Keenetic descriptions: legacy RCI matches a Linux interface by address") {
    KeeneticInterfaceTestState state;
    set_system_info_for_tests(SystemInfo{"keenetic", "4.02.C.1", "keenetic"});
    int calls = 0;
    set_keenetic_interface_fetcher_for_tests([&calls](const std::string& method,
                                                       const std::string&,
                                                       const std::string&) {
        ++calls;
        CHECK(method == "GET");
        return R"({"Wireguard0":{"id":"Wireguard0","description":"Office VPN","address":"10.20.30.1","mask":"255.255.255.0"}})";
    });

    auto response = inventory("wg0", {"10.20.30.1/24"});
    populate_keenetic_interface_descriptions(response);

    REQUIRE(response.interfaces.front().description.has_value());
    CHECK(*response.interfaces.front().description == "Office VPN");
    CHECK(calls == 1);
}

TEST_CASE("Keenetic descriptions: stale refresh failure retains successful cache") {
    KeeneticInterfaceTestState state;
    set_system_info_for_tests(SystemInfo{"keenetic", "4.03", "keenetic"});
    auto now = std::chrono::steady_clock::time_point{};
    set_keenetic_interface_now_fn_for_tests([&now] { return now; });
    set_keenetic_interface_fetcher_for_tests([](const std::string& method,
                                                 const std::string&,
                                                 const std::string&) {
        if (method == "GET") {
            return R"({"GigabitEthernet1":{"id":"GigabitEthernet1","description":"Internet"}})";
        }
        return R"({"show":{"interface":[{"system-name":"eth3"}]}})";
    });

    auto first = inventory("eth3");
    populate_keenetic_interface_descriptions(first);
    REQUIRE(first.interfaces.front().description.has_value());

    now += std::chrono::minutes(2);
    set_keenetic_interface_fetcher_for_tests([](const std::string&, const std::string&, const std::string&) -> std::string {
        throw std::runtime_error("RCI unavailable");
    });
    auto second = inventory("eth3");
    populate_keenetic_interface_descriptions(second);

    REQUIRE(second.interfaces.front().description.has_value());
    CHECK(*second.interfaces.front().description == "Internet");
}

TEST_CASE("Keenetic descriptions: non-Keenetic hosts do not query RCI") {
    KeeneticInterfaceTestState state;
    set_system_info_for_tests(SystemInfo{"openwrt", "24.10", "openwrt"});
    int calls = 0;
    set_keenetic_interface_fetcher_for_tests([&calls](const std::string&, const std::string&, const std::string&) {
        ++calls;
        return std::string{};
    });

    auto response = inventory("eth0");
    populate_keenetic_interface_descriptions(response);

    CHECK_FALSE(response.interfaces.front().description.has_value());
    CHECK(calls == 0);
}

} // namespace keen_pbr3

#endif // WITH_API
