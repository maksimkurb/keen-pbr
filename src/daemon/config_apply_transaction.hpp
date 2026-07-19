#pragma once

#include <stdexcept>
#include <string>

namespace keen_pbr3 {

enum class ConfigApplyTransactionState {
    Prepared,
    CandidateApplied,
    ResolverConfirmed,
    Committed,
    RolledBack,
};

// Encodes the irreversible ordering for config apply. It deliberately owns no
// config data: daemon code remains responsible for kernel and disk effects.
class ConfigApplyTransaction {
public:
    void candidate_applied();
    void resolver_confirmed();
    void committed();
    void rolled_back();

    bool may_commit() const noexcept;
    ConfigApplyTransactionState state() const noexcept { return state_; }

private:
    ConfigApplyTransactionState state_{ConfigApplyTransactionState::Prepared};
};

} // namespace keen_pbr3
