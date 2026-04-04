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
    cfg.fallback = std::vector<std::string>{"default"};
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
    cfg.fallback = std::vector<std::string>{server_tag};
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

TEST_CASE("server= directive includes #port when port != 53") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    const std::string list_name = "mylist";
    auto route_cfg = make_route_cfg(list_name);
    auto dns_cfg   = make_dns_cfg(list_name, "dns1", "8.8.8.8:5353");
    auto lists     = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"example.com"})}};

    DnsServerRegistry reg(dns_cfg);
    DnsmasqGenerator gen(reg, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen);

    CHECK(output.find("server=/example.com/8.8.8.8#5353\n") != std::string::npos);
}

TEST_CASE("server= directive has no #port suffix for default port 53") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    const std::string list_name = "mylist";
    auto route_cfg = make_route_cfg(list_name);
    auto dns_cfg   = make_dns_cfg(list_name, "dns1", "8.8.8.8");
    auto lists     = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"example.com"})}};

    DnsServerRegistry reg(dns_cfg);
    DnsmasqGenerator gen(reg, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen);

    CHECK(output.find("server=/example.com/8.8.8.8\n") != std::string::npos);
    CHECK(output.find("#53") == std::string::npos);
}

TEST_CASE("generate-resolver-config includes fallback server directives in configured order") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    DnsServer primary;
    primary.tag = "primary";
    primary.address = "1.1.1.1";
    DnsServer backup;
    backup.tag = "backup";
    backup.address = "9.9.9.9:5353";

    DnsConfig dns_cfg;
    dns_cfg.servers = std::vector<DnsServer>{primary, backup};
    dns_cfg.fallback = std::vector<std::string>{"primary", "backup"};

    auto route_cfg = make_route_cfg("mylist");
    auto lists = std::map<std::string, ListConfig>{{"mylist", make_list_cfg({"example.com"})}};

    DnsServerRegistry reg(dns_cfg);
    DnsmasqGenerator gen(reg, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen);

    const auto primary_pos = output.find("server=1.1.1.1\n");
    const auto backup_pos = output.find("server=9.9.9.9#5353\n");

    CHECK(primary_pos != std::string::npos);
    CHECK(backup_pos != std::string::npos);
    CHECK(primary_pos < backup_pos);
}

TEST_CASE("hash changes when fallback list order changes") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer1(cache);
    ListStreamer streamer2(cache);

    DnsServer primary;
    primary.tag = "primary";
    primary.address = "1.1.1.1";
    DnsServer backup;
    backup.tag = "backup";
    backup.address = "9.9.9.9";

    DnsConfig dns_cfg1;
    dns_cfg1.servers = std::vector<DnsServer>{primary, backup};
    dns_cfg1.fallback = std::vector<std::string>{"primary", "backup"};

    DnsConfig dns_cfg2;
    dns_cfg2.servers = std::vector<DnsServer>{primary, backup};
    dns_cfg2.fallback = std::vector<std::string>{"backup", "primary"};

    auto route_cfg = make_route_cfg("mylist");
    auto lists = std::map<std::string, ListConfig>{{"mylist", make_list_cfg({"example.com"})}};

    DnsServerRegistry reg1(dns_cfg1);
    DnsServerRegistry reg2(dns_cfg2);
    DnsmasqGenerator gen1(reg1, streamer1, route_cfg, dns_cfg1, lists);
    DnsmasqGenerator gen2(reg2, streamer2, route_cfg, dns_cfg2, lists);

    CHECK(gen1.compute_config_hash() != gen2.compute_config_hash());
}

TEST_CASE("generate-resolver-config includes dns probe server directive when enabled") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    auto route_cfg = make_route_cfg("mylist");
    auto dns_cfg = make_empty_dns_cfg();
    DnsTestServer probe_cfg;
    probe_cfg.listen = "127.0.0.88:53";
    dns_cfg.dns_test_server = probe_cfg;
    auto lists = std::map<std::string, ListConfig>{{"mylist", make_list_cfg({"example.com"})}};

    DnsServerRegistry reg(dns_cfg);
    DnsmasqGenerator gen(reg, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen);

    CHECK(output.find("rebind-domain-ok=keen.pbr\n") != std::string::npos);
    CHECK(output.find("server=/check.keen.pbr/127.0.0.88#53\n") != std::string::npos);
}

TEST_CASE("generate-resolver-config blocks firefox doh canary domain") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    auto route_cfg = make_route_cfg("mylist");
    auto dns_cfg = make_empty_dns_cfg();
    auto lists = std::map<std::string, ListConfig>{{"mylist", make_list_cfg({"example.com"})}};

    DnsServerRegistry reg(dns_cfg);
    DnsmasqGenerator gen(reg, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen);

    CHECK(output.find("address=/use-application-dns.net/\n") != std::string::npos);
}

TEST_CASE("generate-resolver-config omits dns probe server directive when disabled") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    auto route_cfg = make_route_cfg("mylist");
    auto dns_cfg = make_empty_dns_cfg();
    auto lists = std::map<std::string, ListConfig>{{"mylist", make_list_cfg({"example.com"})}};

    DnsServerRegistry reg(dns_cfg);
    DnsmasqGenerator gen(reg, streamer, route_cfg, dns_cfg, lists);
    const std::string output = run_generate(gen);

    CHECK(output.find("rebind-domain-ok=keen.pbr\n") == std::string::npos);
    CHECK(output.find("server=/check.keen.pbr/") == std::string::npos);
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


TEST_CASE("hash changes when resolver type changes") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer1(cache);
    ListStreamer streamer2(cache);

    const std::string list_name = "mylist";
    auto route_cfg = make_route_cfg(list_name);
    auto dns_cfg = make_empty_dns_cfg();
    auto lists = std::map<std::string, ListConfig>{{list_name, make_list_cfg({"example.com"})}};

    DnsServerRegistry reg1(dns_cfg);
    DnsServerRegistry reg2(dns_cfg);

    const std::string ipset_hash = DnsmasqGenerator::compute_config_hash(
        reg1, streamer1, route_cfg, dns_cfg, lists, ResolverType::DNSMASQ_IPSET);
    const std::string nftset_hash = DnsmasqGenerator::compute_config_hash(
        reg2, streamer2, route_cfg, dns_cfg, lists, ResolverType::DNSMASQ_NFTSET);

    CHECK(!ipset_hash.empty());
    CHECK(!nftset_hash.empty());
    CHECK(ipset_hash != nftset_hash);
}

TEST_CASE("ip-only routed list produces no ipset or nftset directives") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    const std::string list_name = "my-ips";
    RouteConfig route_cfg = make_route_cfg(list_name);
    DnsConfig dns_cfg = make_empty_dns_cfg();

    ListConfig list_cfg;
    list_cfg.ip_cidrs = std::vector<std::string>{"10.0.0.1", "192.168.0.0/24"};
    auto lists = std::map<std::string, ListConfig>{{list_name, list_cfg}};

    DnsServerRegistry reg(dns_cfg);

    DnsmasqGenerator ipset_gen(reg, streamer, route_cfg, dns_cfg, lists,
                               ResolverType::DNSMASQ_IPSET);
    const std::string ipset_output = run_generate(ipset_gen);
    CHECK(ipset_output.find("ipset=") == std::string::npos);
    CHECK(ipset_output.find("server=/") == std::string::npos);

    DnsmasqGenerator nftset_gen(reg, streamer, route_cfg, dns_cfg, lists,
                                ResolverType::DNSMASQ_NFTSET);
    const std::string nftset_output = run_generate(nftset_gen);
    CHECK(nftset_output.find("nftset=") == std::string::npos);
    CHECK(nftset_output.find("server=/") == std::string::npos);
}
