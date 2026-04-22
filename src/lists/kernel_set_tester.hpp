#pragma once

#include "../firewall/firewall.hpp"

#include <optional>
#include <string>

namespace keen_pbr3 {

class KernelSetTester {
public:
    explicit KernelSetTester(FirewallBackend backend);

    std::optional<bool> contains(const std::string& set_name,
                                 const std::string& ip) const;

private:
    FirewallBackend backend_;
};

} // namespace keen_pbr3
