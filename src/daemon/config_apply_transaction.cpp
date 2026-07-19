#include "config_apply_transaction.hpp"

namespace keen_pbr3 {
namespace {

void require_state(ConfigApplyTransactionState actual,
                   ConfigApplyTransactionState expected,
                   const char* operation) {
    if (actual != expected) {
        throw std::logic_error(std::string("invalid config apply transaction operation: ") + operation);
    }
}

} // namespace

void ConfigApplyTransaction::candidate_applied() {
    require_state(state_, ConfigApplyTransactionState::Prepared, "candidate_applied");
    state_ = ConfigApplyTransactionState::CandidateApplied;
}

void ConfigApplyTransaction::resolver_confirmed() {
    require_state(state_, ConfigApplyTransactionState::CandidateApplied, "resolver_confirmed");
    state_ = ConfigApplyTransactionState::ResolverConfirmed;
}

void ConfigApplyTransaction::committed() {
    require_state(state_, ConfigApplyTransactionState::ResolverConfirmed, "committed");
    state_ = ConfigApplyTransactionState::Committed;
}

void ConfigApplyTransaction::rolled_back() {
    if (state_ == ConfigApplyTransactionState::Committed ||
        state_ == ConfigApplyTransactionState::RolledBack) {
        throw std::logic_error("invalid config apply transaction rollback");
    }
    state_ = ConfigApplyTransactionState::RolledBack;
}

bool ConfigApplyTransaction::may_commit() const noexcept {
    return state_ == ConfigApplyTransactionState::ResolverConfirmed;
}

} // namespace keen_pbr3
