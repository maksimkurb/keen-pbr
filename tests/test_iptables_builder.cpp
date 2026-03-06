#include <doctest/doctest.h>

#include "../src/firewall/iptables.hpp"
#include "../src/firewall/ipset_restore_pipe.hpp"
#include "../src/lists/list_entry_visitor.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Friend class with test access to IptablesFirewall private methods.
class IptablesBuilderTest {
public:
    // Public mirror of PendingRule for use in test functions.
    struct RuleDesc {
        std::string set_name;
        bool ipv6;
        enum Action { Mark, Drop } action;
        uint32_t fwmark;
    };

    static std::string build_ipset_create_line(const std::string& name,
                                               const std::string& family_str,
                                               uint32_t timeout) {
        IptablesFirewall::PendingSet ps;
        ps.name       = name;
        ps.family_str = family_str;
        ps.timeout    = timeout;
        return IptablesFirewall::build_ipset_create_line(ps);
    }

    static std::string build_ipt_script(bool ipv6, const std::vector<RuleDesc>& descs) {
        std::vector<IptablesFirewall::PendingRule> rules;
        rules.reserve(descs.size());
        for (const auto& d : descs) {
            IptablesFirewall::PendingRule pr;
            pr.set_name = d.set_name;
            pr.ipv6     = d.ipv6;
            pr.action   = (d.action == RuleDesc::Mark) ? IptablesFirewall::PendingRule::Mark
                                                       : IptablesFirewall::PendingRule::Drop;
            pr.fwmark   = d.fwmark;
            rules.push_back(std::move(pr));
        }
        return IptablesFirewall::build_ipt_script(ipv6, rules);
    }
};

} // namespace keen_pbr3

using namespace keen_pbr3;
using T    = IptablesBuilderTest;
using Rule = IptablesBuilderTest::RuleDesc;

static Rule mark_rule(const std::string& set_name, bool ipv6, uint32_t fwmark) {
    return {set_name, ipv6, Rule::Mark, fwmark};
}

static Rule drop_rule(const std::string& set_name, bool ipv6) {
    return {set_name, ipv6, Rule::Drop, 0};
}

// =============================================================================
// IpsetRestoreVisitor::on_entry tests
// =============================================================================

TEST_CASE("IpsetRestoreVisitor: IP entry without timeout") {
    std::ostringstream buf;
    IpsetRestoreVisitor v(buf, "myset", -1);
    v.on_entry(EntryType::Ip, "10.0.0.1");
    CHECK(buf.str() == "add myset 10.0.0.1\n");
    CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: IP entry with timeout=60") {
    std::ostringstream buf;
    IpsetRestoreVisitor v(buf, "myset", 60);
    v.on_entry(EntryType::Ip, "10.0.0.1");
    CHECK(buf.str() == "add myset 10.0.0.1 timeout 60\n");
    CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: CIDR entry") {
    std::ostringstream buf;
    IpsetRestoreVisitor v(buf, "myset", -1);
    v.on_entry(EntryType::Cidr, "192.168.0.0/24");
    CHECK(buf.str() == "add myset 192.168.0.0/24\n");
    CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: Domain entry is ignored") {
    std::ostringstream buf;
    IpsetRestoreVisitor v(buf, "myset", -1);
    v.on_entry(EntryType::Domain, "example.com");
    CHECK(buf.str().empty());
    CHECK(v.count() == 0);
}

TEST_CASE("IpsetRestoreVisitor: count increments only for IP/CIDR") {
    std::ostringstream buf;
    IpsetRestoreVisitor v(buf, "myset", -1);
    v.on_entry(EntryType::Ip,     "1.2.3.4");
    v.on_entry(EntryType::Domain, "example.com");
    v.on_entry(EntryType::Cidr,   "10.0.0.0/8");
    CHECK(v.count() == 2);
}

// =============================================================================
// build_ipset_create_line tests
// =============================================================================

TEST_CASE("build_ipset_create_line: IPv4 without timeout") {
    auto line = T::build_ipset_create_line("myset", "inet", 0);
    CHECK(line == "create myset hash:net family inet -exist\n");
}

TEST_CASE("build_ipset_create_line: IPv4 with timeout 60") {
    auto line = T::build_ipset_create_line("myset", "inet", 60);
    CHECK(line == "create myset hash:net family inet timeout 60 -exist\n");
}

TEST_CASE("build_ipset_create_line: IPv6 without timeout") {
    auto line = T::build_ipset_create_line("myset", "inet6", 0);
    CHECK(line == "create myset hash:net family inet6 -exist\n");
}

// =============================================================================
// build_ipt_script tests
// =============================================================================

TEST_CASE("build_ipt_script: IPv4 mark rule") {
    auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)});
    CHECK(s.find("*mangle") != std::string::npos);
    CHECK(s.find(":KeenPbrTable") != std::string::npos);
    CHECK(s.find("-A PREROUTING -j KeenPbrTable") != std::string::npos);
    CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j MARK --set-mark 0x100") != std::string::npos);
    CHECK(s.size() >= 7);
    CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: IPv4 drop rule") {
    auto s = T::build_ipt_script(false, {drop_rule("blacklist", false)});
    CHECK(s.find("-A KeenPbrTable -m set --match-set blacklist dst -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: IPv6 mark rule") {
    auto s = T::build_ipt_script(true, {mark_rule("v6set", true, 0x200)});
    CHECK(s.find("-A KeenPbrTable -m set --match-set v6set dst -j MARK --set-mark 0x200") != std::string::npos);
    CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: ipv6=false filters out IPv6 rules") {
    auto s = T::build_ipt_script(false, {mark_rule("v4set", false, 0x100),
                                         mark_rule("v6set", true,  0x200)});
    CHECK(s.find("v4set") != std::string::npos);
    CHECK(s.find("v6set") == std::string::npos);
}

TEST_CASE("build_ipt_script: ipv6=true filters out IPv4 rules") {
    auto s = T::build_ipt_script(true, {mark_rule("v4set", false, 0x100),
                                        mark_rule("v6set", true,  0x200)});
    CHECK(s.find("v6set") != std::string::npos);
    CHECK(s.find("v4set") == std::string::npos);
}

TEST_CASE("build_ipt_script: zero fwmark") {
    auto s = T::build_ipt_script(false, {mark_rule("zeroset", false, 0)});
    CHECK(s.find("--set-mark 0x0") != std::string::npos);
}

TEST_CASE("build_ipt_script: multiple rules appear in order") {
    auto s = T::build_ipt_script(false, {mark_rule("first",  false, 0x1),
                                         drop_rule("second", false)});
    auto pos_first  = s.find("first");
    auto pos_second = s.find("second");
    CHECK(pos_first  != std::string::npos);
    CHECK(pos_second != std::string::npos);
    CHECK(pos_first < pos_second);
}
