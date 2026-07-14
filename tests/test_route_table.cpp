#include <doctest/doctest.h>

#include "../src/routing/route_table.hpp"

#include <vector>

namespace keen_pbr3 {
namespace {

class FakeRouteNetlink : public RouteNetlinkOperations {
public:
    RouteAddResult add_route(const RouteSpec& spec) override {
        added.push_back(spec);
        return add_result;
    }
    void delete_route(const RouteSpec& spec) override { deleted.push_back(spec); }

    RouteAddResult add_result{RouteAddResult::Created};
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

} // namespace keen_pbr3
