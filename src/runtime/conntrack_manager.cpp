#include "conntrack_manager.hpp"

#include "../util/safe_exec.hpp"

namespace keen_pbr3 {

ConntrackManager::ConntrackManager(CommandRunner runner)
    : runner_(std::move(runner)) {
    if (!runner_) {
        runner_ = [](const std::vector<std::string>& args) {
            return safe_exec(args, /*suppress_output=*/true);
        };
    }
}

bool ConntrackManager::reconcile(ConntrackPolicy desired) {
    if (active_ == desired) {
        return false;
    }
    active_ = desired;
    return true;
}

ConntrackPolicy ConntrackManager::inspect() const {
    return active_;
}

uint32_t ConntrackManager::restore_original_mark(uint32_t nfmark, uint32_t ctmark,
                                                 uint32_t owned_mask) {
    return (nfmark & ~owned_mask) | (ctmark & owned_mask);
}

uint32_t ConntrackManager::save_selected_mark(uint32_t ctmark, uint32_t nfmark,
                                              uint32_t owned_mask) {
    return (ctmark & ~owned_mask) | (nfmark & owned_mask);
}

bool ConntrackManager::delete_mark(uint32_t mark, uint32_t owned_mask) const {
    const std::string selector = std::to_string(mark) + "/" + std::to_string(owned_mask);
    const bool ipv4 = runner_({"conntrack", "-D", "-f", "ipv4", "--mark", selector}) == 0;
    const bool ipv6 = runner_({"conntrack", "-D", "-f", "ipv6", "--mark", selector}) == 0;
    return ipv4 && ipv6;
}

} // namespace keen_pbr3
