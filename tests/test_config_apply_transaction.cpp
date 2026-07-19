#include <doctest/doctest.h>

#include "../src/daemon/config_apply_transaction.hpp"

using namespace keen_pbr3;

TEST_CASE("config apply transaction commits only after resolver confirmation") {
    ConfigApplyTransaction transaction;
    CHECK_FALSE(transaction.may_commit());
    CHECK_THROWS(transaction.committed());

    transaction.candidate_applied();
    CHECK_FALSE(transaction.may_commit());
    CHECK_THROWS(transaction.committed());

    transaction.resolver_confirmed();
    CHECK(transaction.may_commit());
    transaction.committed();
    CHECK(transaction.state() == ConfigApplyTransactionState::Committed);
}

TEST_CASE("config apply transaction makes rollback terminal") {
    ConfigApplyTransaction transaction;
    transaction.candidate_applied();
    transaction.rolled_back();
    CHECK(transaction.state() == ConfigApplyTransactionState::RolledBack);
    CHECK_THROWS(transaction.resolver_confirmed());
    CHECK_THROWS(transaction.rolled_back());
}
