#include <doctest/doctest.h>

#include "../src/routing/route_table.hpp"

#include <stdexcept>
#include <vector>

namespace keen_pbr3 {
namespace {

class FakeRouteNetlink : public RouteNetlinkOperations {
public:
    RouteAddResult add_route(const RouteSpec& spec) override {
        added.push_back(spec);
        if (fail_add) throw std::runtime_error("injected failure");
        return add_result;
    }
    void delete_route(const RouteSpec& spec) override { deleted.push_back(spec); }

    RouteAddResult add_result{RouteAddResult::Created};
    bool fail_add{false};
    std::vector<RouteSpec> added;
    std::vector<RouteSpec> deleted;
};

RouteSpec route(std::string destination, std::uint32_t table) {
    RouteSpec value;
    value.destination = std::move(destination);
    value.table = table;
    value.blackhole = true;
    return value;
}

} // namespace

TEST_CASE("RouteTable deletes only routes created by this process") {
    FakeRouteNetlink netlink;
    RouteTable routes(netlink);

    const auto owned = route("default", 150);
    routes.add(owned);
    netlink.add_result = RouteAddResult::AlreadyPresent;
    const auto foreign = route("192.0.2.0/24", 150);
    routes.add(foreign);

    REQUIRE(routes.get_routes().size() == 2);
    routes.clear();
    REQUIRE(netlink.deleted.size() == 1);
    CHECK(netlink.deleted.front().destination == owned.destination);
}

TEST_CASE("RouteTable remove preserves an identical pre-existing route") {
    FakeRouteNetlink netlink;
    netlink.add_result = RouteAddResult::AlreadyPresent;
    RouteTable routes(netlink);
    const auto foreign = route("default", 150);
    routes.add(foreign);
    routes.remove(foreign);

    CHECK(netlink.deleted.empty());
    CHECK(routes.get_routes().empty());
}

TEST_CASE("RouteTable propagates route installation failures") {
    FakeRouteNetlink netlink;
    netlink.fail_add = true;
    RouteTable routes(netlink);

    CHECK_THROWS(routes.add(route("default", 150)));
    CHECK(routes.get_routes().empty());
}

TEST_CASE("RouteTable reconciliation adds replacements before removing obsolete routes") {
    FakeRouteNetlink netlink;
    RouteTable routes(netlink);
    const auto old_route = route("192.0.2.0/24", 150);
    const auto new_route = route("198.51.100.0/24", 150);
    routes.add(old_route);
    netlink.added.clear();

    routes.reconcile({new_route});

    REQUIRE(netlink.added.size() == 1);
    CHECK(netlink.added.front().destination == new_route.destination);
    REQUIRE(netlink.deleted.size() == 1);
    CHECK(netlink.deleted.front().destination == old_route.destination);
    CHECK(routes.get_routes().size() == 1);
}

TEST_CASE("RouteTable treats protocol as route identity") {
    FakeRouteNetlink netlink;
    RouteTable routes(netlink);
    auto generated = route("default", 150);
    auto foreign = generated;
    foreign.protocol = 4;

    routes.add(generated);
    routes.add(foreign);

    CHECK(routes.get_routes().size() == 2);
}

TEST_CASE("RouteTable adopts desired state without claiming ownership") {
    FakeRouteNetlink netlink;
    RouteTable routes(netlink);
    const auto expected = route("default", 150);

    routes.adopt_desired({expected});
    routes.clear();

    CHECK(netlink.deleted.empty());
}

} // namespace keen_pbr3
