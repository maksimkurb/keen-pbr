#include <doctest/doctest.h>

#include "runtime/runtime_state_machine.hpp"

namespace keen_pbr3 {

TEST_CASE("RuntimeStateMachine rejects invalid transitions without mutation") {
    RuntimeStateMachine machine;
    std::string error;

    CHECK_FALSE(machine.transition(RuntimeState::applying, "bad", error));
    CHECK(machine.state() == RuntimeState::starting);
    CHECK(error == "invalid runtime transition: starting -> applying");
}

TEST_CASE("RuntimeStateMachine records transitions and recovery from broken") {
    RuntimeStateMachine machine;
    std::string error;

    REQUIRE(machine.transition(RuntimeState::running, "startup complete", error));
    REQUIRE(machine.transition(RuntimeState::applying, "config apply", error));
    REQUIRE(machine.transition(RuntimeState::broken, "rollback failed", error));
    REQUIRE(machine.transition(RuntimeState::applying, "recovery apply", error));
    REQUIRE(machine.transition(RuntimeState::running, "recovery complete", error));
    CHECK(machine.reason() == "recovery complete");
}

TEST_CASE("RuntimeStateMachine prevents mutations while shutting down") {
    RuntimeStateMachine machine{RuntimeState::running};
    std::string error;

    REQUIRE(machine.transition(RuntimeState::shutting_down, "signal", error));
    CHECK_FALSE(machine.transition(RuntimeState::applying, "late mutation", error));
    CHECK(machine.state() == RuntimeState::shutting_down);
}

TEST_CASE("RuntimeStateMachine permits shutdown while startup is asynchronous") {
    RuntimeStateMachine machine;
    std::string error;

    CHECK(machine.transition(RuntimeState::shutting_down, "signal during startup", error));
    CHECK(machine.state() == RuntimeState::shutting_down);
}

} // namespace keen_pbr3
