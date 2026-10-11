#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

class InterfaceMonitorError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class InterfaceMonitor {
public:
    struct Event {
        std::string interface_name;
        bool administrative_state_changed{false};
        bool is_up{false};
        bool route_changed{false};
        bool address_changed{false};
        bool default_route_changed{false};
        // The kernel dropped notifications (receive queue overflow); the
        // monitor resynced its own state, but whatever changed is unknown.
        bool resync{false};
    };
    using InterfaceStateCallback = std::function<void(const Event&)>;

    // Route events have no interface name; auto-gateway users therefore need
    // a separate refresh predicate from link/address events.
    static bool requires_runtime_refresh(const Event& event,
                                         bool interface_outbound_in_use,
                                         bool auto_gateway_outbound,
                                         bool auto_gateway_interface,
                                         bool default_gateway_rules,
                                         bool learning_wan_scope = false) noexcept {
        // learning_wan_scope: interception learns from every client interface
        // except the outbound/WAN ones, which include the main-table
        // default-route interfaces.
        return event.resync ||
               ((default_gateway_rules || learning_wan_scope) &&
                event.default_route_changed) ||
               (event.administrative_state_changed && interface_outbound_in_use) ||
               (event.route_changed && auto_gateway_outbound) ||
               (event.address_changed && auto_gateway_interface);
    }

    explicit InterfaceMonitor(InterfaceStateCallback callback);
    ~InterfaceMonitor();

    InterfaceMonitor(const InterfaceMonitor&) = delete;
    InterfaceMonitor& operator=(const InterfaceMonitor&) = delete;
    InterfaceMonitor(InterfaceMonitor&&) = delete;
    InterfaceMonitor& operator=(InterfaceMonitor&&) = delete;

    int fd() const;
    void handle_events();
    void reconnect();

    // Drops route notifications for every table except main, in the kernel.
    // Returns false when the socket refused the filter.
    static bool attach_main_table_route_filter(int fd) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3
