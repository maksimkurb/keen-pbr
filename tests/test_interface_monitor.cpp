#include "../src/routing/interface_monitor.hpp"

#include <doctest/doctest.h>

namespace keen_pbr3 {

TEST_CASE("InterfaceMonitor reconnect rebuilds usable netlink socket") {
    std::unique_ptr<InterfaceMonitor> monitor;
    try {
        monitor = std::make_unique<InterfaceMonitor>([](const std::string&, bool) {});
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
