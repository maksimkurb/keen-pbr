#pragma once

namespace keen_pbr3 {

enum class ConfigApplyTransactionState {
    Prepared,
    CandidateApplied,
    ResolverConfirmed,
    Committed,
};

// Encodes the irreversible ordering for config apply. It deliberately owns no
// config data: daemon code remains responsible for kernel and disk effects.
class ConfigApplyTransaction {
public:
    void candidate_applied();
    void resolver_confirmed();
    void committed();

    bool may_commit() const noexcept;
    ConfigApplyTransactionState state() const noexcept { return state_; }

private:
    ConfigApplyTransactionState state_{ConfigApplyTransactionState::Prepared};
};

} // namespace keen_pbr3
