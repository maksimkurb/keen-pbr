#include <doctest/doctest.h>

#include "../src/firewall/nftables.hpp"

#include <nlohmann/json.hpp>
#include <sys/socket.h>

// NftablesBuilderTest must be in the same namespace as the friend declaration
// (keen_pbr3::NftablesBuilderTest matches the friend class NftablesBuilderTest
// declared inside namespace keen_pbr3).
namespace keen_pbr3 {

// Friend class with test access to NftablesFirewall private methods.
// Helper methods take plain parameters and build the private structs internally,
// because friend access applies inside member function bodies, not at class scope.
class NftablesBuilderTest {
public:
    // set helpers
    static nlohmann::json build_table_json() {
        return NftablesFirewall::build_table_json();
    }

    static nlohmann::json build_set_json(const std::string& name, const std::string& type,
                                         uint32_t timeout) {
        NftablesFirewall::PendingSet ps;
        ps.name    = name;
        ps.type    = type;
        ps.timeout = timeout;
        return NftablesFirewall::build_set_json(ps);
    }

    static nlohmann::json build_chain_json() {
        return NftablesFirewall::build_chain_json();
    }

    static nlohmann::json build_mark_rule_json(const std::string& set_name, int family,
                                               uint32_t fwmark,
                                               ProtoPortFilter filter = {}) {
        NftablesFirewall::PendingRule pr;
        pr.set_name = set_name;
        pr.family   = family;
        pr.action   = NftablesFirewall::PendingRule::Mark;
        pr.fwmark   = fwmark;
        pr.filter   = filter;
        return NftablesFirewall::build_mark_rule_json(pr);
    }

    static nlohmann::json build_drop_rule_json(const std::string& set_name, int family,
                                               ProtoPortFilter filter = {}) {
        NftablesFirewall::PendingRule pr;
        pr.set_name = set_name;
        pr.family   = family;
        pr.action   = NftablesFirewall::PendingRule::Drop;
        pr.fwmark   = 0;
        pr.filter   = filter;
        return NftablesFirewall::build_drop_rule_json(pr);
    }

    static nlohmann::json build_port_match_exprs(const std::string& proto,
                                                  const std::string& src_port,
                                                  const std::string& dst_port) {
        return NftablesFirewall::build_port_match_exprs(proto, src_port, dst_port);
    }

    static nlohmann::json build_addr_match_exprs(const std::string& ip_proto,
                                                  const std::vector<std::string>& src_addr,
                                                  const std::vector<std::string>& dst_addr) {
        return NftablesFirewall::build_addr_match_exprs(ip_proto, src_addr, dst_addr);
    }

    static nlohmann::json build_elements_json(const std::string& set_name,
                                              const nlohmann::json& elems) {
        return NftablesFirewall::build_elements_json(set_name, elems);
    }
};

} // namespace keen_pbr3

using namespace keen_pbr3;
using T = NftablesBuilderTest;

// =============================================================================
// build_set_json tests
// =============================================================================

TEST_CASE("build_set_json: IPv4 without timeout") {
    auto j = T::build_set_json("myset", "ipv4_addr", 0);
    const auto& set = j["add"]["set"];
    CHECK(set["type"] == "ipv4_addr");
    CHECK(set["flags"].is_array());
    CHECK(set["flags"].size() == 1);
    CHECK(set["flags"][0] == "interval");
    CHECK(set["auto-merge"] == true);
    CHECK_FALSE(set.contains("timeout"));
}

TEST_CASE("build_set_json: IPv4 with timeout") {
    auto j = T::build_set_json("timeset", "ipv4_addr", 60);
    const auto& set = j["add"]["set"];
    CHECK(set["flags"].is_array());
    CHECK(set["flags"].size() == 2);
    CHECK(set["flags"][0] == "interval");
    CHECK(set["flags"][1] == "timeout");
    CHECK(set["auto-merge"] == true);
    CHECK(set.contains("timeout"));
    CHECK(set["timeout"] == 60);
}

TEST_CASE("build_set_json: IPv6 set") {
    auto j = T::build_set_json("v6set", "ipv6_addr", 0);
    CHECK(j["add"]["set"]["type"] == "ipv6_addr");
}

// =============================================================================
// build_chain_json tests
// =============================================================================

TEST_CASE("build_chain_json: correct fields") {
    auto j = T::build_chain_json();
    const auto& chain = j["add"]["chain"];
    CHECK(chain["family"] == "inet");
    CHECK(chain["table"]  == "KeenPbrTable");
    CHECK(chain["name"]   == "prerouting");
    CHECK(chain["hook"]   == "prerouting");
    CHECK(chain["prio"]   == -150);
    CHECK(chain["policy"] == "accept");
}

// =============================================================================
// build_mark_rule_json tests
// =============================================================================

TEST_CASE("build_mark_rule_json: IPv4 mark rule") {
    auto j = T::build_mark_rule_json("myset", AF_INET, 256);
    const auto& expr = j["add"]["rule"]["expr"];

    CHECK(expr[0]["match"]["left"]["payload"]["protocol"] == "ip");
    CHECK(expr[0]["match"]["left"]["payload"]["field"]    == "daddr");
    CHECK(expr[0]["match"]["right"]                       == "@myset");
    CHECK(expr[1].contains("counter"));
    CHECK(expr[2]["mangle"]["key"]["meta"]["key"] == "mark");
    CHECK(expr[2]["mangle"]["value"]              == 256);
}

TEST_CASE("build_mark_rule_json: IPv6 mark rule") {
    auto j = T::build_mark_rule_json("v6set", AF_INET6, 512);
    CHECK(j["add"]["rule"]["expr"][0]["match"]["left"]["payload"]["protocol"] == "ip6");
}

TEST_CASE("build_mark_rule_json: zero fwmark is valid") {
    auto j = T::build_mark_rule_json("zeroset", AF_INET, 0);
    CHECK(j["add"]["rule"]["expr"][2]["mangle"]["value"] == 0);
}

// =============================================================================
// build_drop_rule_json tests
// =============================================================================

TEST_CASE("build_drop_rule_json: IPv4 drop rule") {
    auto j = T::build_drop_rule_json("blacklist", AF_INET);
    const auto& expr = j["add"]["rule"]["expr"];

    CHECK(expr[0]["match"]["left"]["payload"]["protocol"] == "ip");

    bool has_drop = false, has_mangle = false;
    for (const auto& e : expr) {
        if (e.contains("drop"))   has_drop   = true;
        if (e.contains("mangle")) has_mangle = true;
    }
    CHECK(has_drop);
    CHECK_FALSE(has_mangle);
}

TEST_CASE("build_drop_rule_json: IPv6 drop rule") {
    auto j = T::build_drop_rule_json("v6bl", AF_INET6);
    CHECK(j["add"]["rule"]["expr"][0]["match"]["left"]["payload"]["protocol"] == "ip6");

    bool has_drop = false;
    for (const auto& e : j["add"]["rule"]["expr"]) {
        if (e.contains("drop")) has_drop = true;
    }
    CHECK(has_drop);
}

// =============================================================================
// build_elements_json tests
// =============================================================================

TEST_CASE("build_elements_json: correct set name, table, and elements") {
    nlohmann::json elems = nlohmann::json::array({"10.0.0.1", "10.0.0.2"});
    auto j = T::build_elements_json("myset", elems);
    const auto& elem = j["add"]["element"];
    CHECK(elem["family"] == "inet");
    CHECK(elem["table"]  == "KeenPbrTable");
    CHECK(elem["name"]   == "myset");
    CHECK(elem["elem"]   == elems);
}

// =============================================================================
// Schema validity tests
// =============================================================================

TEST_CASE("full document structure: top-level key is nftables array") {
    nlohmann::json doc;
    auto& arr = doc["nftables"];
    arr = nlohmann::json::array();
    arr.push_back({{"metainfo", {{"json_schema_version", 1}}}});
    arr.push_back(T::build_table_json());
    arr.push_back({{"delete", {{"table", {{"family", "inet"}, {"name", "KeenPbrTable"}}}}}});
    arr.push_back(T::build_table_json());
    arr.push_back(T::build_chain_json());

    CHECK(doc.contains("nftables"));
    CHECK(doc["nftables"].is_array());
}

TEST_CASE("each command object has expected top-level key") {
    nlohmann::json meta = {{"metainfo", {{"json_schema_version", 1}}}};
    CHECK(meta.contains("metainfo"));

    auto add_table = T::build_table_json();
    CHECK(add_table.contains("add"));
    CHECK(add_table["add"].contains("table"));

    nlohmann::json del = {{"delete", {{"table", {{"family", "inet"}, {"name", "KeenPbrTable"}}}}}};
    CHECK(del.contains("delete"));
    CHECK(del["delete"].contains("table"));

    auto chain = T::build_chain_json();
    CHECK(chain.contains("add"));
    CHECK(chain["add"].contains("chain"));

    auto set_j = T::build_set_json("s", "ipv4_addr", 0);
    CHECK(set_j.contains("add"));
    CHECK(set_j["add"].contains("set"));

    auto rule_j = T::build_mark_rule_json("s", AF_INET, 1);
    CHECK(rule_j.contains("add"));
    CHECK(rule_j["add"].contains("rule"));

    auto elem_j = T::build_elements_json("s", nlohmann::json::array({"1.2.3.4"}));
    CHECK(elem_j.contains("add"));
    CHECK(elem_j["add"].contains("element"));
}

// =============================================================================
// build_port_match_exprs tests
// =============================================================================

TEST_CASE("build_port_match_exprs: empty filter → empty array") {
    auto exprs = T::build_port_match_exprs("", "", "");
    CHECK(exprs.is_array());
    CHECK(exprs.empty());
}

TEST_CASE("build_port_match_exprs: proto only → l4proto match") {
    auto exprs = T::build_port_match_exprs("tcp", "", "");
    REQUIRE(exprs.size() == 1);
    CHECK(exprs[0]["match"]["left"]["meta"]["key"] == "l4proto");
    CHECK(exprs[0]["match"]["right"] == "tcp");
}

TEST_CASE("build_port_match_exprs: tcp + single dest_port → port integer") {
    auto exprs = T::build_port_match_exprs("tcp", "", "443");
    REQUIRE(exprs.size() == 2);
    CHECK(exprs[0]["match"]["left"]["meta"]["key"] == "l4proto");
    CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "dport");
    CHECK(exprs[1]["match"]["right"] == 443);
}

TEST_CASE("build_port_match_exprs: udp + port range → range JSON") {
    auto exprs = T::build_port_match_exprs("udp", "", "8000-9000");
    REQUIRE(exprs.size() == 2);
    const auto& rhs = exprs[1]["match"]["right"];
    CHECK(rhs.contains("range"));
    CHECK(rhs["range"][0] == 8000);
    CHECK(rhs["range"][1] == 9000);
}

TEST_CASE("build_port_match_exprs: port list → set JSON") {
    auto exprs = T::build_port_match_exprs("tcp", "", "80,443");
    REQUIRE(exprs.size() == 2);
    const auto& rhs = exprs[1]["match"]["right"];
    CHECK(rhs.contains("set"));
    CHECK(rhs["set"][0] == 80);
    CHECK(rhs["set"][1] == 443);
}

TEST_CASE("build_port_match_exprs: src_port + dest_port → two port exprs") {
    auto exprs = T::build_port_match_exprs("tcp", "1024", "443");
    // l4proto + sport + dport
    REQUIRE(exprs.size() == 3);
    CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "sport");
    CHECK(exprs[2]["match"]["left"]["payload"]["field"] == "dport");
}

// =============================================================================
// build_mark_rule_json with proto/port filter tests
// =============================================================================

TEST_CASE("build_mark_rule_json: tcp + dest_port=443 → port match expr present") {
    ProtoPortFilter f; f.proto = "tcp"; f.dst_port = "443";
    auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
    const auto& expr = j["add"]["rule"]["expr"];
    // expr[0]=daddr match, expr[1]=l4proto, expr[2]=dport, expr[3]=counter, expr[4]=mangle
    bool has_dport = false;
    for (const auto& e : expr) {
        if (e.contains("match") && e["match"]["left"].contains("payload") &&
            e["match"]["left"]["payload"]["field"] == "dport") {
            has_dport = true;
        }
    }
    CHECK(has_dport);
}

TEST_CASE("build_mark_rule_json: no filter → no port exprs (regression)") {
    auto j = T::build_mark_rule_json("myset", AF_INET, 0x100);
    const auto& expr = j["add"]["rule"]["expr"];
    // Should be exactly 3: daddr match, counter, mangle
    CHECK(expr.size() == 3);
}

TEST_CASE("build_drop_rule_json: no filter → no port exprs (regression)") {
    auto j = T::build_drop_rule_json("bl", AF_INET);
    const auto& expr = j["add"]["rule"]["expr"];
    CHECK(expr.size() == 3);
}

// =============================================================================
// build_addr_match_exprs tests
// =============================================================================

TEST_CASE("build_addr_match_exprs: both empty → empty array") {
    auto exprs = T::build_addr_match_exprs("ip", {}, {});
    CHECK(exprs.is_array());
    CHECK(exprs.empty());
}

TEST_CASE("build_addr_match_exprs: single src_addr → saddr match with string") {
    auto exprs = T::build_addr_match_exprs("ip", {"192.168.10.0/24"}, {});
    REQUIRE(exprs.size() == 1);
    CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
    CHECK(exprs[0]["match"]["right"] == "192.168.10.0/24");
}

TEST_CASE("build_addr_match_exprs: multiple src_addr → saddr match with set") {
    auto exprs = T::build_addr_match_exprs("ip", {"192.168.1.0/24", "10.0.0.0/8"}, {});
    REQUIRE(exprs.size() == 1);
    CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
    CHECK(exprs[0]["match"]["right"].contains("set"));
    CHECK(exprs[0]["match"]["right"]["set"][0] == "192.168.1.0/24");
    CHECK(exprs[0]["match"]["right"]["set"][1] == "10.0.0.0/8");
}

TEST_CASE("build_addr_match_exprs: single dst_addr → daddr match with string") {
    auto exprs = T::build_addr_match_exprs("ip", {}, {"10.0.0.0/8"});
    REQUIRE(exprs.size() == 1);
    CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "daddr");
    CHECK(exprs[0]["match"]["right"] == "10.0.0.0/8");
}

TEST_CASE("build_addr_match_exprs: src_addr + dst_addr → two match exprs") {
    auto exprs = T::build_addr_match_exprs("ip", {"192.168.1.0/24"}, {"8.8.8.0/24"});
    REQUIRE(exprs.size() == 2);
    CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
    CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "daddr");
}

TEST_CASE("build_addr_match_exprs: ip6 family → ip6 protocol in payload") {
    auto exprs = T::build_addr_match_exprs("ip6", {"fd00::/8"}, {});
    REQUIRE(exprs.size() == 1);
    CHECK(exprs[0]["match"]["left"]["payload"]["protocol"] == "ip6");
}

// =============================================================================
// build_mark_rule_json with src_addr / dest_addr filter tests
// =============================================================================

TEST_CASE("build_mark_rule_json: src_addr → saddr expr present") {
    ProtoPortFilter f;
    f.src_addr = {"192.168.10.0/24"};
    auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
    const auto& expr = j["add"]["rule"]["expr"];
    // daddr @set, saddr match, counter, mangle = 4
    CHECK(expr.size() == 4);
    bool has_saddr = false;
    for (const auto& e : expr) {
        if (e.contains("match") && e["match"]["left"].contains("payload") &&
            e["match"]["left"]["payload"]["field"] == "saddr") {
            has_saddr = true;
            CHECK(e["match"]["right"] == "192.168.10.0/24");
        }
    }
    CHECK(has_saddr);
}

TEST_CASE("build_mark_rule_json: multiple src_addr → set literal in saddr expr") {
    ProtoPortFilter f;
    f.src_addr = {"192.168.1.0/24", "10.0.0.0/8"};
    auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
    const auto& expr = j["add"]["rule"]["expr"];
    bool has_set_saddr = false;
    for (const auto& e : expr) {
        if (e.contains("match") && e["match"]["left"].contains("payload") &&
            e["match"]["left"]["payload"]["field"] == "saddr") {
            has_set_saddr = e["match"]["right"].contains("set");
        }
    }
    CHECK(has_set_saddr);
}

TEST_CASE("build_mark_rule_json: no filter → still exactly 3 exprs (regression)") {
    auto j = T::build_mark_rule_json("myset", AF_INET, 0x100);
    const auto& expr = j["add"]["rule"]["expr"];
    CHECK(expr.size() == 3);
}
