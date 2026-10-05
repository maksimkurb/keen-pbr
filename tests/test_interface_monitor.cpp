#include "../src/routing/interface_monitor.hpp"

#include <doctest/doctest.h>

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
