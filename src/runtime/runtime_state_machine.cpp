#include "runtime_state_machine.hpp"

#include <utility>

namespace keen_pbr3 {

const char* runtime_state_name(RuntimeState state) {
    switch (state) {
    case RuntimeState::starting: return "starting";
    case RuntimeState::running: return "running";
    case RuntimeState::restart_required: return "restart_required";
    case RuntimeState::applying: return "applying";
    case RuntimeState::stopped: return "stopped";
    case RuntimeState::broken: return "broken";
    case RuntimeState::shutting_down: return "shutting_down";
    }
    return "unknown";
}

RuntimeStateMachine::RuntimeStateMachine(RuntimeState initial) : state_(initial) {}

RuntimeState RuntimeStateMachine::state() const { return state_; }

const std::string& RuntimeStateMachine::reason() const { return reason_; }

bool RuntimeStateMachine::transition(RuntimeState next, std::string reason, std::string& error) {
    const bool allowed =
        (state_ == RuntimeState::starting &&
         (next == RuntimeState::running || next == RuntimeState::stopped || next == RuntimeState::broken)) ||
        (state_ == RuntimeState::running &&
         (next == RuntimeState::applying || next == RuntimeState::restart_required ||
          next == RuntimeState::stopped || next == RuntimeState::broken || next == RuntimeState::shutting_down)) ||
        (state_ == RuntimeState::restart_required &&
         (next == RuntimeState::applying || next == RuntimeState::stopped || next == RuntimeState::broken ||
          next == RuntimeState::shutting_down)) ||
        (state_ == RuntimeState::applying &&
         (next == RuntimeState::running || next == RuntimeState::broken || next == RuntimeState::stopped)) ||
        (state_ == RuntimeState::stopped &&
         (next == RuntimeState::starting || next == RuntimeState::applying || next == RuntimeState::broken ||
          next == RuntimeState::shutting_down)) ||
        (state_ == RuntimeState::broken &&
         (next == RuntimeState::applying || next == RuntimeState::stopped || next == RuntimeState::shutting_down)) ||
        (state_ == RuntimeState::shutting_down && next == RuntimeState::stopped);
    if (!allowed) {
        error = std::string("invalid runtime transition: ") + runtime_state_name(state_) + " -> " +
                runtime_state_name(next);
        return false;
    }
    state_ = next;
    reason_ = std::move(reason);
    return true;
}

} // namespace keen_pbr3
