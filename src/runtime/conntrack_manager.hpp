#pragma once

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
    // Records the policy successfully handed to the firewall backend.
    // The backend owns the kernel representation; this class owns the
    // reconciler-visible desired/actual policy snapshot.
    bool reconcile(ConntrackPolicy desired);
    ConntrackPolicy inspect() const;

private:
    ConntrackPolicy active_;
};

} // namespace keen_pbr3
