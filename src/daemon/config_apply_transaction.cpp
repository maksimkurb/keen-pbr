#include "config_apply_transaction.hpp"

#include <stdexcept>
#include <string>

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

void ConfigApplyTransaction::runtime_confirmed() {
    require_state(state_, ConfigApplyTransactionState::CandidateApplied, "runtime_confirmed");
    state_ = ConfigApplyTransactionState::RuntimeConfirmed;
}

void ConfigApplyTransaction::committed() {
    require_state(state_, ConfigApplyTransactionState::RuntimeConfirmed, "committed");
    state_ = ConfigApplyTransactionState::Committed;
}

bool ConfigApplyTransaction::may_commit() const noexcept {
    return state_ == ConfigApplyTransactionState::RuntimeConfirmed;
}

} // namespace keen_pbr3
