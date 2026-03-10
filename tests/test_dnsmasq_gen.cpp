#include <doctest/doctest.h>

#include "../src/dns/dnsmasq_gen.hpp"
#include "../src/dns/dns_router.hpp"
#include "../src/cache/cache_manager.hpp"
#include "../src/lists/list_streamer.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace keen_pbr3;

// =============================================================================
// Test helpers
// =============================================================================

// Build a minimal RouteConfig that references list_name under a route rule.
static RouteConfig make_route_cfg(const std::string& list_name) {
    RouteRule rule;
    rule.list     = std::vector<std::string>{list_name};
    rule.outbound = "direct";
    RouteConfig cfg;
    cfg.rules = std::vector<RouteRule>{rule};
    return cfg;
}

// Build a minimal valid DnsConfig with no rules (one fallback server required by DnsServerRegistry).
static DnsConfig make_empty_dns_cfg() {
    DnsServer srv;
    srv.tag     = "default";
    srv.address = "127.0.0.1";
    DnsConfig cfg;
    cfg.fallback = "default";
    cfg.servers  = std::vector<DnsServer>{srv};
    return cfg;
}

// Build a DnsConfig with a single server and a rule mapping list_name to that server.
static DnsConfig make_dns_cfg(const std::string& list_name,
                               const std::string& server_tag,
                               const std::string& server_ip) {
    DnsServer srv;
    srv.tag     = server_tag;
    srv.address = server_ip;
    DnsRule rule;
    rule.list   = std::vector<std::string>{list_name};
    rule.server = server_tag;
    DnsConfig cfg;
    cfg.fallback = server_tag;
    cfg.servers  = std::vector<DnsServer>{srv};
    cfg.rules    = std::vector<DnsRule>{rule};
    return cfg;
}

// Build a ListConfig with inline domains.
static ListConfig make_list_cfg(std::vector<std::string> domains) {
    ListConfig cfg;
    cfg.domains = std::move(domains);
    return cfg;
}

// Run generate() and return the full output string.
static std::string run_generate(DnsmasqGenerator& gen) {
    std::ostringstream oss;
    gen.generate(oss);
    return oss.str();
}

// Extract the hash from the txt-record line in generate() output.
static std::string extract_txt_hash(const std::string& output) {
    const std::string prefix = "txt-record=config-hash.keen.pbr,";
    auto pos = output.rfind(prefix);  // rfind: TXT line is last
    if (pos == std::string::npos) return "";
    pos += prefix.size();
    auto end = output.find('\n', pos);
    return output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

// =============================================================================
// Dynamic set naming tests (dnsmasq ipset=/nftset= directives)
// =============================================================================
//
// dnsmasq must write resolved IPs into the *dynamic* sets (kpbr4d_* / kpbr6d_*)
// so that TTL-based expiry can be applied. Static IPs (from ip_cidrs/file/url)
// are loaded into the static sets (kpbr4_* / kpbr6_*) by the daemon directly.

TEST_CASE("dnsmasq set names: IPv4 dynamic set uses kpbr4d_ prefix") {
    CHECK(DnsmasqGenerator::ipset_name_v4("mylist") == "kpbr4d_mylist");
}

TEST_CASE("dnsmasq set names: IPv6 dynamic set uses kpbr6d_ prefix") {
    CHECK(DnsmasqGenerator::ipset_name_v6("mylist") == "kpbr6d_mylist");
}

TEST_CASE("dnsmasq set names: list name with underscores and hyphens") {
    CHECK(DnsmasqGenerator::ipset_name_v4("my-list_01") == "kpbr4d_my-list_01");
    CHECK(DnsmasqGenerator::ipset_name_v6("my-list_01") == "kpbr6d_my-list_01");
}

TEST_CASE("dnsmasq set names: static sets do NOT use kpbr4d_ prefix") {
    // Static set names are constructed in daemon.cpp as "kpbr4_" + list_name
    // and are distinct from what DnsmasqGenerator emits for dnsmasq directives.
    const std::string list = "example";
    const std::string dynamic_v4 = DnsmasqGenerator::ipset_name_v4(list);
    const std::string dynamic_v6 = DnsmasqGenerator::ipset_name_v6(list);
    const std::string static_v4  = "kpbr4_"  + list;
    const std::string static_v6  = "kpbr6_"  + list;
    CHECK(dynamic_v4 != static_v4);
    CHECK(dynamic_v6 != static_v6);
}

// =============================================================================
// Hash consistency and coverage tests
// =============================================================================

TEST_CASE("compute_config_hash matches hash embedded in generate() output") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    const std::string list_name = "testlist";
    const std::string server_tag = "dns1";
    const std::string server_ip = "8.8.8.8";

    auto dns_cfg   = make_dns_cfg(list_name, server_tag, server_ip);
    auto route_cfg = make_route_cfg(list_name);
    auto lists     = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"example.com", "foo.test"})}};

    DnsServerRegistry dns_registry(dns_cfg);
    DnsmasqGenerator gen(dns_registry, streamer, route_cfg, dns_cfg, lists);

    const std::string instance_hash = gen.compute_config_hash();

    // Re-create generator (list_streamer state is reset)
    DnsServerRegistry dns_registry2(dns_cfg);
    DnsmasqGenerator gen2(dns_registry2, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen2);
    const std::string embedded_hash = extract_txt_hash(output);

    CHECK(!instance_hash.empty());
    CHECK(instance_hash == embedded_hash);
}

TEST_CASE("txt-record line is the last line in generate() output") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    const std::string list_name = "mylist";
    auto route_cfg = make_route_cfg(list_name);
    auto lists     = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"alpha.example", "beta.example"})}};
    auto dns_cfg = make_empty_dns_cfg();
    DnsServerRegistry dns_registry(dns_cfg);
    DnsmasqGenerator gen(dns_registry, streamer, route_cfg, dns_cfg, lists);

    const std::string output = run_generate(gen);

    // Find last non-empty line
    std::string last_line;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty()) last_line = line;
    }

    CHECK(last_line.rfind("txt-record=config-hash.keen.pbr,", 0) == 0);
}

TEST_CASE("hash changes when server IP changes") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer1(cache);
    ListStreamer streamer2(cache);

    const std::string list_name = "mylist";
    auto route_cfg = make_route_cfg(list_name);
    auto lists     = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"example.com"})}};

    auto dns_cfg1 = make_dns_cfg(list_name, "dns1", "1.1.1.1");
    auto dns_cfg2 = make_dns_cfg(list_name, "dns1", "9.9.9.9");

    DnsServerRegistry reg1(dns_cfg1);
    DnsServerRegistry reg2(dns_cfg2);
    DnsmasqGenerator gen1(reg1, streamer1, route_cfg, dns_cfg1, lists);
    DnsmasqGenerator gen2(reg2, streamer2, route_cfg, dns_cfg2, lists);

    const std::string hash1 = gen1.compute_config_hash();
    const std::string hash2 = gen2.compute_config_hash();

    CHECK(!hash1.empty());
    CHECK(!hash2.empty());
    CHECK(hash1 != hash2);
}

TEST_CASE("hash changes when domain list content changes") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer1(cache);
    ListStreamer streamer2(cache);

    const std::string list_name = "mylist";
    auto route_cfg = make_route_cfg(list_name);
    auto dns_cfg = make_empty_dns_cfg();
    DnsServerRegistry reg1(dns_cfg);
    DnsServerRegistry reg2(dns_cfg);

    auto lists1 = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"a.example"})}};
    auto lists2 = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"b.example"})}};

    DnsmasqGenerator gen1(reg1, streamer1, route_cfg, dns_cfg, lists1);
    DnsmasqGenerator gen2(reg2, streamer2, route_cfg, dns_cfg, lists2);

    const std::string hash1 = gen1.compute_config_hash();
    const std::string hash2 = gen2.compute_config_hash();

    CHECK(!hash1.empty());
    CHECK(!hash2.empty());
    CHECK(hash1 != hash2);
}
