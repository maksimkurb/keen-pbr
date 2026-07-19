#include <doctest/doctest.h>

#include "runtime/operation_coordinator.hpp"

namespace keen_pbr3 {

TEST_CASE("OperationCoordinator rejects concurrent mutation immediately") {
    OperationCoordinator coordinator;
    CHECK(coordinator.try_begin("config apply"));
    CHECK(coordinator.busy());
    CHECK(coordinator.operation() == "config apply");
    CHECK_FALSE(coordinator.try_begin("list refresh"));
    coordinator.finish();
    CHECK_FALSE(coordinator.busy());
    CHECK(coordinator.try_begin("list refresh"));
}

} // namespace keen_pbr3
