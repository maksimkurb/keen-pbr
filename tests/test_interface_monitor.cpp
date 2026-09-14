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
    CHECK(InterfaceMonitor::requires_runtime_refresh(address_event, false, false, false, true));
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
