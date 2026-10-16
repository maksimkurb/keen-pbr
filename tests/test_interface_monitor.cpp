#include "../src/routing/interface_monitor.hpp"
#include "../src/routing/netlink.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

namespace keen_pbr3 {

TEST_CASE("InterfaceMonitor refresh predicate handles auto gateways on routes and addresses") {
    InterfaceMonitor::Event route_event;
    route_event.route_changed = true;
    CHECK(InterfaceMonitor::requires_runtime_refresh(route_event, false, true, false, false));
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(route_event, false, false, false, false));

    InterfaceMonitor::Event address_event;
    address_event.address_changed = true;
    CHECK(InterfaceMonitor::requires_runtime_refresh(address_event, false, false, true, false));
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(address_event, false, false, false, false));
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(address_event, false, false, false, true));
}

TEST_CASE("InterfaceMonitor refresh predicate for default_gateway rules requires default route change") {
    // Non-default route change with default_gateway rules should NOT trigger refresh
    InterfaceMonitor::Event non_default_route_event;
    non_default_route_event.route_changed = true;
    non_default_route_event.default_route_changed = false;
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(non_default_route_event, false, false, false, true));

    // Default route change with default_gateway rules should trigger refresh
    InterfaceMonitor::Event default_route_event;
    default_route_event.route_changed = true;
    default_route_event.default_route_changed = true;
    CHECK(InterfaceMonitor::requires_runtime_refresh(default_route_event, false, false, false, true));

    // Unrelated link event (e.g., administrative state change) should NOT trigger refresh with default_gateway rules
    InterfaceMonitor::Event link_event;
    link_event.interface_name = "eth0";
    link_event.administrative_state_changed = true;
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(link_event, false, false, false, true));
}

TEST_CASE("InterfaceMonitor refresh predicate for the learning WAN scope follows the default route") {
    // The WAN interfaces excluded from learning include the default-route one.
    InterfaceMonitor::Event default_route_event;
    default_route_event.route_changed = true;
    default_route_event.default_route_changed = true;
    CHECK(InterfaceMonitor::requires_runtime_refresh(default_route_event, false, false, false, false, true));
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(default_route_event, false, false, false, false, false));

    InterfaceMonitor::Event other_route_event;
    other_route_event.route_changed = true;
    CHECK_FALSE(InterfaceMonitor::requires_runtime_refresh(other_route_event, false, false, false, false, true));
}

TEST_CASE("InterfaceMonitor refresh predicate always refreshes after a resync") {
    InterfaceMonitor::Event resync_event;
    resync_event.resync = true;
    CHECK(InterfaceMonitor::requires_runtime_refresh(resync_event, false, false, false, false));
}

namespace {

// Sends one route/link notification through a socket carrying the filter and
// reports whether it came out.  A unix datagram pair runs the same kernel
// classic BPF as a netlink socket and needs no privileges.
bool route_filter_passes(uint16_t type, uint8_t table) {
    int fds[2] = {-1, -1};
    REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, fds) == 0);
    REQUIRE(InterfaceMonitor::attach_main_table_route_filter(fds[1]));

    std::array<unsigned char, NLMSG_SPACE(sizeof(rtmsg))> buffer{};
    auto* header = reinterpret_cast<nlmsghdr*>(buffer.data());
    header->nlmsg_len = buffer.size();
    header->nlmsg_type = type;
    auto* route = static_cast<rtmsg*>(NLMSG_DATA(header));
    route->rtm_family = AF_INET;
    route->rtm_table = table;
    REQUIRE(send(fds[0], buffer.data(), buffer.size(), 0) ==
            static_cast<ssize_t>(buffer.size()));

    std::array<unsigned char, 256> received{};
    const ssize_t n = recv(fds[1], received.data(), received.size(), 0);
    close(fds[0]);
    close(fds[1]);
    return n == static_cast<ssize_t>(buffer.size());
}

DumpedRoute route(std::string destination, std::string gateway) {
    DumpedRoute result;
    result.destination = std::move(destination);
    result.table = 254;
    result.family = AF_INET;
    result.gateway = std::move(gateway);
    result.interface = "eth3";
    result.metric = 1000;
    return result;
}

} // namespace

TEST_CASE("InterfaceMonitor route filter keeps only main-table routes") {
    CHECK(route_filter_passes(RTM_NEWROUTE, RT_TABLE_MAIN));
    CHECK(route_filter_passes(RTM_DELROUTE, RT_TABLE_MAIN));
    CHECK_FALSE(route_filter_passes(RTM_NEWROUTE, RT_TABLE_LOCAL));
    CHECK_FALSE(route_filter_passes(RTM_DELROUTE, RT_TABLE_COMPAT));
    CHECK_FALSE(route_filter_passes(RTM_NEWROUTE, 100));
    // Link and address notifications pass whatever their payload holds.
    CHECK(route_filter_passes(RTM_NEWLINK, 100));
    CHECK(route_filter_passes(RTM_NEWADDR, 0));
}

TEST_CASE("same_routes ignores dump order and sees any field change") {
    const std::vector<DumpedRoute> applied{route("default", "100.124.0.1"),
                                           route("85.21.2.1", "100.124.0.1")};
    const std::vector<DumpedRoute> reordered{applied[1], applied[0]};
    CHECK(same_routes(applied, reordered));

    auto new_gateway = reordered;
    new_gateway[1].gateway = "100.124.0.2";
    CHECK_FALSE(same_routes(applied, new_gateway));

    auto new_metric = reordered;
    new_metric[0].metric = 10;
    CHECK_FALSE(same_routes(applied, new_metric));

    CHECK_FALSE(same_routes(applied, {applied[0]}));
    CHECK_FALSE(same_routes(applied, {applied[0], applied[0]}));
}

TEST_CASE("InterfaceMonitor reconnect rebuilds usable netlink socket") {
    std::unique_ptr<InterfaceMonitor> monitor;
    try {
        monitor = std::make_unique<InterfaceMonitor>([](const InterfaceMonitor::Event&) {});
    } catch (const InterfaceMonitorError& e) {
        (void)e;
        return;
    }

    CHECK(monitor->fd() >= 0);
    CHECK_NOTHROW(monitor->handle_events());

    CHECK_NOTHROW(monitor->reconnect());
    CHECK(monitor->fd() >= 0);
    CHECK_NOTHROW(monitor->handle_events());
}

} // namespace keen_pbr3
