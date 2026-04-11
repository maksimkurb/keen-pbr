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
    using InterfaceStateCallback = std::function<void(const std::string& interface_name, bool is_up)>;

    explicit InterfaceMonitor(InterfaceStateCallback callback);
    ~InterfaceMonitor();

    InterfaceMonitor(const InterfaceMonitor&) = delete;
    InterfaceMonitor& operator=(const InterfaceMonitor&) = delete;
    InterfaceMonitor(InterfaceMonitor&&) = delete;
    InterfaceMonitor& operator=(InterfaceMonitor&&) = delete;

    int fd() const;
    void handle_events();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3
