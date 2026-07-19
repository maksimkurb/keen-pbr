#include <doctest/doctest.h>

#include "../src/routing/policy_rule.hpp"

#include <netinet/in.h>
#include <stdexcept>
#include <vector>

namespace keen_pbr3 {
namespace {

class FakeRuleNetlink : public RuleNetlinkOperations {
public:
    RuleAddResult add_rule_for_family(const RuleSpec&, int family) override {
        added.push_back(family);
        if (family == failing_family) throw std::runtime_error("injected failure");
        return family == existing_family ? RuleAddResult::AlreadyPresent
                                         : RuleAddResult::Created;
    }
    void delete_rule_for_family(const RuleSpec&, int family) override {
        deleted.push_back(family);
    }

    int failing_family{0};
    int existing_family{0};
    std::vector<int> added;
    std::vector<int> deleted;
};

RuleSpec dual_stack_rule() {
    RuleSpec rule;
    rule.fwmark = 10;
    rule.table = 110;
    rule.priority = 10010;
    return rule;
}

} // namespace

TEST_CASE("PolicyRuleManager rolls back IPv4 when IPv6 add fails") {
    FakeRuleNetlink netlink;
    netlink.failing_family = AF_INET6;
    PolicyRuleManager rules(netlink);

    CHECK_THROWS(rules.add(dual_stack_rule()));
    CHECK(rules.size() == 0);
    REQUIRE(netlink.deleted.size() == 1);
    CHECK(netlink.deleted.front() == AF_INET);
}

TEST_CASE("PolicyRuleManager does not delete a pre-existing rule during rollback") {
    FakeRuleNetlink netlink;
    netlink.existing_family = AF_INET;
    netlink.failing_family = AF_INET6;
    PolicyRuleManager rules(netlink);

    CHECK_THROWS(rules.add(dual_stack_rule()));
    CHECK(netlink.deleted.empty());
}

TEST_CASE("PolicyRuleManager clears only concrete rules it created") {
    FakeRuleNetlink netlink;
    netlink.existing_family = AF_INET6;
    PolicyRuleManager rules(netlink);
    rules.add(dual_stack_rule());
    rules.clear();

    REQUIRE(netlink.deleted.size() == 1);
    CHECK(netlink.deleted.front() == AF_INET);
}

TEST_CASE("PolicyRuleManager reconciliation leaves an identical plan untouched") {
    FakeRuleNetlink netlink;
    PolicyRuleManager rules(netlink);
    const auto rule = dual_stack_rule();
    rules.add(rule);
    netlink.added.clear();
    netlink.deleted.clear();

    rules.reconcile({rule});

    CHECK(netlink.added.empty());
    CHECK(netlink.deleted.empty());
}

TEST_CASE("PolicyRuleManager adopts desired state without claiming ownership") {
    FakeRuleNetlink netlink;
    PolicyRuleManager rules(netlink);

    rules.adopt_desired({dual_stack_rule()});
    rules.clear();

    CHECK(netlink.deleted.empty());
}

} // namespace keen_pbr3
