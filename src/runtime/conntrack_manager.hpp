#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Conntrack handling is consumed by firewall backends, but its policy has a
// separate lifecycle and must be observable independently of generated rules.
struct ConntrackPolicy {
    bool bypass_established_or_dnat{false};

    bool operator==(const ConntrackPolicy& other) const {
        return bypass_established_or_dnat == other.bypass_established_or_dnat;
    }
    bool operator!=(const ConntrackPolicy& other) const { return !(*this == other); }
};

class ConntrackManager {
public:
    using CommandRunner = std::function<int(const std::vector<std::string>&)>;

    explicit ConntrackManager(CommandRunner runner = {});
    // Records the policy successfully handed to the firewall backend.
    // The backend owns the kernel representation; this class owns the
    // reconciler-visible desired/actual policy snapshot.
    bool reconcile(ConntrackPolicy desired);
    ConntrackPolicy inspect() const;

    // Restore only keen-pbr owned bits for an original-direction packet. Reply
    // traffic deliberately bypasses restore so it cannot be classified again.
    static uint32_t restore_original_mark(uint32_t nfmark, uint32_t ctmark,
                                          uint32_t owned_mask);
    static uint32_t save_selected_mark(uint32_t ctmark, uint32_t nfmark,
                                       uint32_t owned_mask);

    // Best-effort, targeted removal for an obsolete owned mark. It never
    // flushes the global conntrack table and invokes each IP family separately.
    bool delete_mark(uint32_t mark, uint32_t owned_mask) const;

private:
    ConntrackPolicy active_;
    CommandRunner runner_;
};

} // namespace keen_pbr3
