#include <doctest/doctest.h>

#include "../src/dns/dnsmasq_gen.hpp"
#include "../src/dns/dns_router.hpp"
#include "../src/dns/keenetic_dns.hpp"
#include "../src/cache/cache_manager.hpp"
#include "../src/crypto/md5.hpp"
#include "../src/lists/list_streamer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

using namespace keen_pbr3;

namespace {

struct KeeneticDnsTestStateGuard {
    KeeneticDnsTestStateGuard() { reset_keenetic_dns_test_state(); }
    ~KeeneticDnsTestStateGuard() { reset_keenetic_dns_test_state(); }
};

DnsServer make_server(const std::string& tag, const std::string& address) {
    DnsServer srv;
    srv.tag = tag;
    srv.address = address;
    return srv;
}

DnsRule make_rule(const std::string& list_name, const std::string& server_tag,
                  bool allow_domain_rebinding = false) {
    DnsRule rule;
    rule.list = std::vector<std::string>{list_name};
    rule.server = server_tag;
    rule.allow_domain_rebinding = allow_domain_rebinding;
    return rule;
}

// A DnsConfig with a single server and a rule mapping list_name to it.
DnsConfig make_dns_cfg(const std::string& list_name,
                       const std::string& server_tag,
                       const std::string& server_ip,
                       bool allow_domain_rebinding = false) {
    DnsConfig cfg;
    cfg.servers = std::vector<DnsServer>{make_server(server_tag, server_ip)};
    cfg.rules = std::vector<DnsRule>{make_rule(list_name, server_tag, allow_domain_rebinding)};
    return cfg;
}

ListConfig make_list_cfg(std::vector<std::string> domains) {
    ListConfig cfg;
    cfg.domains = std::move(domains);
    return cfg;
}

struct Generated {
    std::string output;
    std::string hash;
    DnsmasqGenStats stats;
};

Generated run_generate(const DnsConfig& dns_cfg,
                       const std::map<std::string, ListConfig>& lists) {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    DnsServerRegistry registry(dns_cfg);
    DnsmasqGenerator gen(registry, streamer, dns_cfg, lists);
    std::ostringstream oss;
    Generated result;
    result.hash = gen.generate(oss, &result.stats);
    result.output = oss.str();
    return result;
}

std::vector<std::string> lines_with_prefix(const std::string& output, const std::string& prefix) {
    std::vector<std::string> found;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) found.push_back(line);
    }
    return found;
}

// "server=/a/b/c/ip" -> {a, b, c}
std::vector<std::string> domains_of_server_line(const std::string& line,
                                                const std::string& server_addr) {
    const std::string prefix = "server=";
    const std::string suffix = "/" + server_addr;
    if (line.rfind(prefix, 0) != 0 || line.size() < prefix.size() + suffix.size() ||
        line.substr(line.size() - suffix.size()) != suffix) {
        return {};
    }
    const std::string path = line.substr(prefix.size(), line.size() - prefix.size() - suffix.size());
    std::vector<std::string> domains;
    std::string current;
    for (const char ch : path) {
        if (ch == '/') {
            if (!current.empty()) domains.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) domains.push_back(current);
    return domains;
}

std::string make_domain_with_len(size_t target_len, const std::string& seed) {
    std::string domain = seed;
    while (domain.size() < target_len) {
        const size_t remaining = target_len - domain.size();
        const size_t label_len = std::min<size_t>(63, remaining - 1);
        domain.push_back('.');
        domain.append(label_len, 'a');
    }
    return domain;
}

} // namespace

TEST_CASE("dnsmasq gen: first line is the generated header") {
    const auto gen = run_generate(make_dns_cfg("mylist", "dns1", "1.1.1.1"),
                                  {{"mylist", make_list_cfg({"example.com"})}});
    CHECK(gen.output.rfind("# keen-pbr generated, do not edit\n", 0) == 0);
    CHECK(gen.output.find("address=/use-application-dns.net/\n") != std::string::npos);
}

TEST_CASE("dnsmasq gen: basic rule emits scoped server= line") {
    const auto gen = run_generate(make_dns_cfg("mylist", "dns1", "1.1.1.1"),
                                  {{"mylist", make_list_cfg({"example.com", "other.org"})}});
    CHECK(gen.output.find("server=/example.com/other.org/1.1.1.1\n") != std::string::npos);
    CHECK(gen.output.find("no-resolv") == std::string::npos);
    CHECK(gen.stats.rules == 1);
    CHECK(gen.stats.domains == 2);
    CHECK(gen.hash.size() == 32);
}

TEST_CASE("dnsmasq gen: hash covers exactly the written bytes") {
    const auto gen = run_generate(make_dns_cfg("mylist", "dns1", "1.1.1.1"),
                                  {{"mylist", make_list_cfg({"example.com"})}});
    // Independent MD5 of the output through the same helper used by the generator.
    crypto::detail::MD5State md5;
    md5.update(reinterpret_cast<const uint8_t*>(gen.output.data()), gen.output.size());
    CHECK(gen.hash == crypto::digest_to_hex(md5.digest()));
}

TEST_CASE("dnsmasq gen: multiple rules and servers") {
    DnsConfig cfg;
    cfg.servers = std::vector<DnsServer>{make_server("a", "1.1.1.1"), make_server("b", "9.9.9.9")};
    cfg.rules = std::vector<DnsRule>{make_rule("l1", "a"), make_rule("l2", "b")};
    const auto gen = run_generate(cfg, {{"l1", make_list_cfg({"one.example"})},
                                        {"l2", make_list_cfg({"two.example"})}});
    CHECK(gen.output.find("server=/one.example/1.1.1.1\n") != std::string::npos);
    CHECK(gen.output.find("server=/two.example/9.9.9.9\n") != std::string::npos);
    CHECK(gen.stats.rules == 2);
    CHECK(gen.stats.domains == 2);
}

TEST_CASE("dnsmasq gen: the first enabled rule naming a list decides its upstream") {
    DnsConfig cfg;
    cfg.servers = std::vector<DnsServer>{make_server("a", "1.1.1.1")};
    cfg.rules = std::vector<DnsRule>{make_rule("l1", "a"), make_rule("l1", "missing")};
    const auto gen = run_generate(cfg, {{"l1", make_list_cfg({"one.example"})}});
    // The first rule naming a list wins.
    CHECK(lines_with_prefix(gen.output, "server=/one.example/").size() == 1);
}

TEST_CASE("dnsmasq gen: disabled rules are skipped") {
    auto cfg = make_dns_cfg("mylist", "dns1", "8.8.8.8", true);
    cfg.rules->at(0).enabled = false;
    const auto gen = run_generate(cfg, {{"mylist", make_list_cfg({"example.com"})}});
    CHECK(gen.output.find("server=/example.com/") == std::string::npos);
    CHECK(gen.output.find("rebind-domain-ok=") == std::string::npos);
    CHECK(gen.stats.rules == 0);
    CHECK(gen.stats.domains == 0);
}

TEST_CASE("dnsmasq gen: wildcard prefix is stripped") {
    const auto gen = run_generate(make_dns_cfg("wild", "dns1", "1.1.1.1"),
                                  {{"wild", make_list_cfg({"*.google.com"})}});
    CHECK(gen.output.find("server=/google.com/1.1.1.1\n") != std::string::npos);
    CHECK(gen.output.find("*.google.com") == std::string::npos);
}

TEST_CASE("dnsmasq gen: non-default port uses #port, default port has no suffix") {
    {
        const auto gen = run_generate(make_dns_cfg("l", "d", "8.8.8.8:5353"),
                                      {{"l", make_list_cfg({"example.com"})}});
        CHECK(gen.output.find("server=/example.com/8.8.8.8#5353\n") != std::string::npos);
    }
    {
        const auto gen = run_generate(make_dns_cfg("l", "d", "8.8.8.8"),
                                      {{"l", make_list_cfg({"example.com"})}});
        CHECK(gen.output.find("server=/example.com/8.8.8.8\n") != std::string::npos);
        CHECK(gen.output.find("#53") == std::string::npos);
    }
}

TEST_CASE("dnsmasq gen: IPv6 upstream is written without brackets") {
    const auto gen = run_generate(make_dns_cfg("l", "d", "[2001:db8::1]:5353"),
                                  {{"l", make_list_cfg({"example.com"})}});
    CHECK(gen.output.find("server=/example.com/2001:db8::1#5353\n") != std::string::npos);

    const auto plain = run_generate(make_dns_cfg("l", "d", "2001:db8::1"),
                                    {{"l", make_list_cfg({"example.com"})}});
    CHECK(plain.output.find("server=/example.com/2001:db8::1\n") != std::string::npos);
}

TEST_CASE("dnsmasq gen: rebind-domain-ok follows allow_domain_rebinding") {
    const auto lists = std::map<std::string, ListConfig>{
        {"mylist", make_list_cfg({"example.com", "*.lan.test"})}};
    const auto on = run_generate(make_dns_cfg("mylist", "dns1", "8.8.8.8", true), lists);
    CHECK(on.output.find("rebind-domain-ok=/example.com/lan.test/\n") != std::string::npos);
    const auto off = run_generate(make_dns_cfg("mylist", "dns1", "8.8.8.8", false), lists);
    CHECK(off.output.find("rebind-domain-ok=") == std::string::npos);
}

TEST_CASE("dnsmasq gen: fallback adds no-resolv and unscoped servers in order") {
    DnsConfig cfg;
    cfg.servers = std::vector<DnsServer>{make_server("a", "1.1.1.1"), make_server("b", "9.9.9.9:5353")};
    cfg.fallback = std::vector<std::string>{"b", "a"};
    const auto gen = run_generate(cfg, {});
    CHECK(gen.output.find("no-resolv\n") != std::string::npos);
    const auto b = gen.output.find("server=9.9.9.9#5353\n");
    const auto a = gen.output.find("server=1.1.1.1\n");
    REQUIRE(a != std::string::npos);
    REQUIRE(b != std::string::npos);
    CHECK(b < a);
}

TEST_CASE("dnsmasq gen: no fallback means no no-resolv") {
    const auto gen = run_generate(make_dns_cfg("l", "d", "8.8.8.8"),
                                  {{"l", make_list_cfg({"example.com"})}});
    CHECK(gen.output.find("no-resolv") == std::string::npos);
    CHECK(lines_with_prefix(gen.output, "server=").size() == 1);
}

TEST_CASE("dnsmasq gen: keenetic-type server expands to its upstreams") {
    KeeneticDnsTestStateGuard guard;
    set_keenetic_dns_fetcher_for_tests([]() {
        return std::string(R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = 198.51.100.10 .\ndns_server = 127.0.0.1:40500 . # tls://resolver.example\ndns_server = 127.0.0.1:40508 . # https://resolver.example/dns-query@dnsm\n"
            }
          ]
        })");
    });

    DnsServer keenetic_server;
    keenetic_server.tag = "keenetic";
    keenetic_server.type = api::DnsServerType::KEENETIC;

    DnsConfig cfg;
    cfg.servers = std::vector<DnsServer>{keenetic_server};
    cfg.fallback = std::vector<std::string>{"keenetic"};
    cfg.rules = std::vector<DnsRule>{make_rule("mylist", "keenetic")};
    const auto gen = run_generate(cfg, {{"mylist", make_list_cfg({"example.com"})}});

    const auto dot_pos = gen.output.find("server=127.0.0.1#40500\n");
    const auto doh_pos = gen.output.find("server=127.0.0.1#40508\n");
    CHECK(dot_pos != std::string::npos);
    CHECK(doh_pos != std::string::npos);
    CHECK(dot_pos < doh_pos);
    CHECK(gen.output.find("server=198.51.100.10\n") == std::string::npos);
    CHECK(gen.output.find("server=/example.com/127.0.0.1#40500\n") != std::string::npos);
    CHECK(gen.output.find("server=/example.com/127.0.0.1#40508\n") != std::string::npos);
}

TEST_CASE("dnsmasq gen: hash is deterministic and tracks content") {
    const auto lists_a = std::map<std::string, ListConfig>{{"l", make_list_cfg({"a.example"})}};
    const auto lists_b = std::map<std::string, ListConfig>{{"l", make_list_cfg({"b.example"})}};
    const auto base = make_dns_cfg("l", "d", "1.1.1.1");

    const auto first = run_generate(base, lists_a);
    const auto second = run_generate(base, lists_a);
    CHECK(first.hash == second.hash);
    CHECK(first.output == second.output);

    CHECK(run_generate(base, lists_b).hash != first.hash);
    CHECK(run_generate(make_dns_cfg("l", "d", "9.9.9.9"), lists_a).hash != first.hash);
    CHECK(run_generate(make_dns_cfg("l", "d", "1.1.1.1", true), lists_a).hash != first.hash);

    auto with_fallback = base;
    with_fallback.fallback = std::vector<std::string>{"d"};
    CHECK(run_generate(with_fallback, lists_a).hash != first.hash);
}

TEST_CASE("dnsmasq gen: 1000 short domains respect batch and line limits") {
    std::vector<std::string> domains;
    std::set<std::string> expected;
    for (int i = 1; i <= 1000; ++i) {
        domains.push_back("d" + std::to_string(i) + ".gg");
        expected.insert(domains.back());
    }
    const auto gen = run_generate(make_dns_cfg("mylist", "dns1", "1.1.1.1"),
                                  {{"mylist", make_list_cfg(domains)}});

    const auto rows = lines_with_prefix(gen.output, "server=/");
    CHECK(rows.size() >= 20);
    std::set<std::string> emitted;
    for (const auto& row : rows) {
        CHECK(row.size() <= 1024);
        const auto row_domains = domains_of_server_line(row, "1.1.1.1");
        CHECK(row_domains.size() <= 50);
        emitted.insert(row_domains.begin(), row_domains.end());
    }
    CHECK(emitted == expected);
    CHECK(gen.stats.domains == 1000);
}

TEST_CASE("dnsmasq gen: long domains split rows below 1024 chars and keep all domains") {
    for (size_t variable_len = 200; variable_len <= 253; ++variable_len) {
        CAPTURE(variable_len);
        std::vector<std::string> domains;
        std::set<std::string> expected;
        domains.push_back(make_domain_with_len(variable_len, "a0"));
        for (int i = 1; i <= 9; ++i) {
            domains.push_back(make_domain_with_len(200, "z" + std::to_string(i)));
        }
        expected.insert(domains.begin(), domains.end());

        const auto gen = run_generate(make_dns_cfg("mylist", "dns1", "1.1.1.1"),
                                      {{"mylist", make_list_cfg(domains)}});
        std::set<std::string> emitted;
        for (const auto& row : lines_with_prefix(gen.output, "server=/")) {
            CHECK(row.size() <= 1024);
            const auto row_domains = domains_of_server_line(row, "1.1.1.1");
            emitted.insert(row_domains.begin(), row_domains.end());
        }
        CHECK(emitted == expected);
    }
}

TEST_CASE("dnsmasq gen: domains longer than 255 chars are skipped") {
    const std::string invalid = std::string(256, 'a') + ".com";
    const auto gen = run_generate(make_dns_cfg("mylist", "dns1", "1.1.1.1"),
                                  {{"mylist", make_list_cfg({"valid.example.com", invalid})}});
    CHECK(gen.output.find("valid.example.com") != std::string::npos);
    CHECK(gen.output.find(invalid) == std::string::npos);
    CHECK(gen.stats.domains == 1);
}

TEST_CASE("dnsmasq gen: cached content is merged with file and inline entries") {
    const auto temp_root =
        std::filesystem::temp_directory_path() / "keen-pbr-test-dnsmasq-cache-merged";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove_all(temp_root, ec);
    };

    try {
        const auto local_file = temp_root / "list.txt";
        {
            std::ofstream local(local_file);
            REQUIRE(local.is_open());
            local << "from-file.example\n";
        }

        CacheManager cache(temp_root);
        {
            std::ofstream cached(cache.cache_path("mylist"));
            REQUIRE(cached.is_open());
            cached << "from-cache.example\n";
        }

        ListConfig list_cfg;
        list_cfg.url = "https://example.com/list.txt";
        list_cfg.file = local_file;
        list_cfg.domains = std::vector<std::string>{"from-inline.example"};

        const auto dns_cfg = make_dns_cfg("mylist", "dns1", "1.1.1.1");
        const std::map<std::string, ListConfig> lists{{"mylist", list_cfg}};
        ListStreamer streamer(cache);
        DnsServerRegistry reg(dns_cfg);
        DnsmasqGenerator gen(reg, streamer, dns_cfg, lists);
        std::ostringstream out;
        gen.generate(out);

        CHECK(out.str().find("from-cache.example") != std::string::npos);
        CHECK(out.str().find("from-file.example") != std::string::npos);
        CHECK(out.str().find("from-inline.example") != std::string::npos);
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }
}

TEST_CASE("dnsmasq stamp: write and parse round trip") {
    const DnsmasqConfigStamp stamp{"0123456789abcdef0123456789abcdef", 123456789, 1700000000};
    std::ostringstream out;
    write_dnsmasq_config_stamp(out, stamp);
    CHECK(out.str() ==
          "txt-record=config-hash.keen.pbr,0123456789abcdef0123456789abcdef|123456789|1700000000\n");

    const std::string line = out.str();
    const std::string value = line.substr(line.find(',') + 1, line.size() - line.find(',') - 2);
    const auto parsed = parse_dnsmasq_config_stamp(value);
    REQUIRE(parsed.has_value());
    CHECK(parsed->hash == stamp.hash);
    CHECK(parsed->boottime_ms == stamp.boottime_ms);
    CHECK(parsed->unix_ts == stamp.unix_ts);
}

TEST_CASE("dnsmasq stamp: strict parser rejects malformed values") {
    const std::string hash = "0123456789abcdef0123456789abcdef";
    CHECK(parse_dnsmasq_config_stamp(hash + "|1|2").has_value());
    CHECK(parse_dnsmasq_config_stamp(hash + "|0|0").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp("").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash).has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|1").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|1|").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "||2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|1|2|3").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|a|2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|-1|2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|1 |2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|99999999999999999999|2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp("0123456789ABCDEF0123456789abcdef|1|2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp("0123456789abcdef0123456789abcde|1|2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp("0123456789abcdef0123456789abcdefg|1|2").has_value());
    CHECK_FALSE(parse_dnsmasq_config_stamp(hash + "|1|2\n").has_value());
}

namespace {
class DiscardStreamBuf : public std::streambuf {
protected:
    int_type overflow(int_type c) override { return traits_type::not_eof(c); }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};
} // namespace

TEST_CASE("dnsmasq stamp: appended stamp line is not part of the hash") {
    const DnsConfig dns = make_dns_cfg("mylist", "dns1", "1.1.1.1");
    const std::map<std::string, ListConfig> lists{{"mylist", make_list_cfg({"example.com"})}};

    // conf-script path: generate() then the stamp, straight to the stream.
    auto script = run_generate(dns, lists);
    std::ostringstream script_out;
    script_out << script.output;
    write_dnsmasq_config_stamp(script_out, {script.hash, 1, 2});

    // Daemon path: hash computed with nothing stored.
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const DnsServerRegistry registry(dns);
    DnsmasqGenerator gen(registry, streamer, dns, lists);
    DiscardStreamBuf discard;
    std::ostream null_out(&discard);
    CHECK(gen.generate(null_out) == script.hash);

    CHECK(script_out.str().size() > script.output.size());
    CHECK(script_out.str().rfind(script.output, 0) == 0);
}
