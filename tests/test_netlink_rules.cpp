#include <doctest/doctest.h>

#include "../src/routing/netlink.hpp"

#include <linux/fib_rules.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netlink/attr.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/route/rule.h>
#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <optional>

using namespace keen_pbr3;

namespace {

struct MsgDeleter {
    void operator()(struct nl_msg* msg) const {
        if (msg != nullptr) nlmsg_free(msg);
    }
};
using MsgPtr = std::unique_ptr<struct nl_msg, MsgDeleter>;

struct RuleMsg {
    std::uint8_t action{FR_ACT_TO_TBL};
    std::uint8_t flags{0};
    std::uint8_t header_table{0};
    std::optional<std::uint32_t> priority;
    std::optional<std::uint32_t> fwmark;
    std::optional<std::uint32_t> fwmask;
    std::optional<std::uint32_t> table;
    std::optional<std::uint32_t> goto_target;
};

MsgPtr build(const RuleMsg& rule) {
    MsgPtr msg(nlmsg_alloc_simple(RTM_NEWRULE, NLM_F_MULTI));
    REQUIRE(msg != nullptr);
    nlmsg_set_proto(msg.get(), NETLINK_ROUTE);
    struct fib_rule_hdr hdr{};
    hdr.family = AF_INET;
    hdr.action = rule.action;
    hdr.flags = rule.flags;
    hdr.table = rule.header_table;
    REQUIRE(nlmsg_append(msg.get(), &hdr, sizeof(hdr), NLMSG_ALIGNTO) == 0);
    if (rule.priority) REQUIRE(nla_put_u32(msg.get(), FRA_PRIORITY, *rule.priority) == 0);
    if (rule.fwmark) REQUIRE(nla_put_u32(msg.get(), FRA_FWMARK, *rule.fwmark) == 0);
    if (rule.fwmask) REQUIRE(nla_put_u32(msg.get(), FRA_FWMASK, *rule.fwmask) == 0);
    if (rule.table) REQUIRE(nla_put_u32(msg.get(), FRA_TABLE, *rule.table) == 0);
    if (rule.goto_target) REQUIRE(nla_put_u32(msg.get(), FRA_GOTO, *rule.goto_target) == 0);
    return msg;
}

DumpedRule convert(const RuleMsg& rule) {
    const MsgPtr msg = build(rule);
    const auto dumped = netlink_detail::dumped_rule_from_message(msg.get());
    REQUIRE(dumped.has_value());
    return *dumped;
}

struct LibnlView {
    std::uint32_t mark{0};
    std::uint32_t mask{0};
};

// What libnl itself reports for the message.
LibnlView libnl_view(const RuleMsg& rule) {
    const MsgPtr msg = build(rule);
    LibnlView view;
    const int err = nl_msg_parse(msg.get(), [](struct nl_object* obj, void* arg) {
        auto* out = static_cast<LibnlView*>(arg);
        auto* parsed = reinterpret_cast<struct rtnl_rule*>(obj);
        out->mark = rtnl_rule_get_mark(parsed);
        out->mask = rtnl_rule_get_mask(parsed);
    }, &view);
    REQUIRE(err >= 0);
    return view;
}

}  // namespace

TEST_CASE("netlink rules: a plain and a not rule with the same key keep their own flag") {
    RuleMsg plain;
    plain.priority = 110;
    plain.fwmark = 0x1bd00;
    plain.fwmask = 0xffffffffU;
    plain.table = 7120;
    RuleMsg inverted = plain;
    inverted.flags = FIB_RULE_INVERT;

    const DumpedRule a = convert(plain);
    const DumpedRule b = convert(inverted);
    CHECK_FALSE(a.invert);
    CHECK(b.invert);
    CHECK(a.priority == b.priority);
    CHECK(a.fwmark == b.fwmark);
    CHECK(a.fwmask == b.fwmask);
    CHECK(a.table == b.table);
    CHECK(a.action == RuleAction::lookup);
    CHECK(a.family == AF_INET);
    CHECK(a.priority == 110);
    CHECK(a.fwmark == 0x1bd00);
    CHECK(a.fwmask == 0xffffffffU);
    CHECK(a.table == 7120);
}

TEST_CASE("netlink rules: a missing fwmark attribute reads as libnl reads it") {
    RuleMsg rule;
    rule.priority = 32766;
    rule.table = 254;
    rule.flags = FIB_RULE_INVERT;
    const LibnlView expected = libnl_view(rule);
    const DumpedRule dumped = convert(rule);
    CHECK(dumped.fwmark == expected.mark);
    CHECK(dumped.fwmask == expected.mask);
    CHECK(dumped.invert);
}

TEST_CASE("netlink rules: goto and nop are flagged") {
    RuleMsg jump;
    jump.priority = 100;
    jump.action = FR_ACT_GOTO;
    jump.goto_target = 32000;
    const DumpedRule j = convert(jump);
    CHECK_FALSE(j.nop);
    REQUIRE(j.goto_target.has_value());
    CHECK(*j.goto_target == 32000);

    RuleMsg nop;
    nop.priority = 100;
    nop.action = FR_ACT_NOP;
    const DumpedRule n = convert(nop);
    CHECK(n.nop);
    CHECK_FALSE(n.goto_target.has_value());

    RuleMsg lookup;
    lookup.priority = 100;
    lookup.table = 100;
    const DumpedRule l = convert(lookup);
    CHECK_FALSE(l.nop);
    CHECK_FALSE(l.goto_target.has_value());
}

TEST_CASE("netlink rules: the table comes from the header or from FRA_TABLE") {
    RuleMsg small;
    small.priority = 100;
    small.header_table = RT_TABLE_MAIN;
    CHECK(convert(small).table == static_cast<std::uint32_t>(RT_TABLE_MAIN));

    RuleMsg large;
    large.priority = 100;
    large.header_table = RT_TABLE_COMPAT;
    large.table = 7120;
    CHECK(convert(large).table == 7120);
}

TEST_CASE("netlink rules: actions map to RuleAction") {
    RuleMsg rule;
    rule.priority = 100;
    rule.action = FR_ACT_BLACKHOLE;
    CHECK(convert(rule).action == RuleAction::blackhole);
    rule.action = FR_ACT_UNREACHABLE;
    CHECK(convert(rule).action == RuleAction::unreachable);
    // Not reported as "lookup 0".
    rule.action = FR_ACT_PROHIBIT;
    CHECK(convert(rule).action == RuleAction::unreachable);
}

TEST_CASE("netlink rules: other message types are not rules") {
    MsgPtr msg(nlmsg_alloc_simple(RTM_NEWROUTE, 0));
    REQUIRE(msg != nullptr);
    nlmsg_set_proto(msg.get(), NETLINK_ROUTE);
    struct fib_rule_hdr hdr{};
    REQUIRE(nlmsg_append(msg.get(), &hdr, sizeof(hdr), NLMSG_ALIGNTO) == 0);
    CHECK_FALSE(netlink_detail::dumped_rule_from_message(msg.get()).has_value());
}
