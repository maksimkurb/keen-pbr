#include "kernel_set_tester.hpp"

#include "../util/safe_exec.hpp"

namespace keen_pbr3 {

KernelSetTester::KernelSetTester(FirewallBackend backend)
    : backend_(backend) {}

std::optional<bool> KernelSetTester::contains(const std::string& set_name,
                                              const std::string& ip) const {
    int exit_code = -1;
    if (backend_ == FirewallBackend::nftables) {
        exit_code = safe_exec({"nft", "get", "element", "inet", "KeenPbrTable",
                               set_name, "{", ip, "}"},
                              /*suppress_output=*/true);
    } else {
        exit_code = safe_exec({"ipset", "test", set_name, ip},
                              /*suppress_output=*/true);
    }

    if (exit_code == 127) {
        return std::nullopt;
    }
    return exit_code == 0;
}

} // namespace keen_pbr3
