#include "conntrack_manager.hpp"

#include "../util/safe_exec.hpp"

namespace keen_pbr3 {

ConntrackManager::ConntrackManager(CommandRunner runner)
    : runner_(std::move(runner)) {
    if (!runner_) {
        runner_ = [](const std::vector<std::string>& args) {
            constexpr size_t kMaxDiagnosticBytes = 1024;
            const auto result = safe_exec_capture(args,
                                                  /*suppress_stderr=*/false,
                                                  kMaxDiagnosticBytes,
                                                  /*merge_stderr=*/true);
            return CommandResult{result.exit_code, result.stdout_output};
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
    const auto delete_family = [this, &selector](const char* family) {
        const auto result = runner_({"conntrack", "-D", "-f", family, "--mark", selector});
        if (result.exit_code == 0) {
            return true;
        }

        // conntrack exits with status 1 when the selector matches no flows.
        // Cleanup is intentionally idempotent, so an already-empty table is
        // success rather than a best-effort cleanup failure.
        return result.exit_code == 1 &&
               result.output.find("0 flow entries have been deleted") != std::string::npos;
    };
    const bool ipv4 = delete_family("ipv4");
    const bool ipv6 = delete_family("ipv6");
    return ipv4 && ipv6;
}

} // namespace keen_pbr3
