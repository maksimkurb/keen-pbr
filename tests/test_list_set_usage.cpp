#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/lists/list_set_usage.hpp"
#include "../src/lists/list_streamer.hpp"

using namespace keen_pbr3;

TEST_CASE("analyze_list_set_usage: ip-only list creates static sets only") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    ListConfig cfg;
    cfg.ip_cidrs = std::vector<std::string>{"10.0.0.1", "192.168.0.0/24"};
    cfg.ttl_ms = 60000;

    const auto usage = analyze_list_set_usage("ip-only", cfg, streamer);

    CHECK(usage.has_static_entries);
    CHECK_FALSE(usage.has_domain_entries);
    CHECK(usage.dynamic_timeout == 60);
}

TEST_CASE("analyze_list_set_usage: domain-only list creates dynamic sets only") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    ListConfig cfg;
    cfg.domains = std::vector<std::string>{"example.com", "*.example.org"};
    cfg.ttl_ms = 120000;

    const auto usage = analyze_list_set_usage("domain-only", cfg, streamer);

    CHECK_FALSE(usage.has_static_entries);
    CHECK(usage.has_domain_entries);
    CHECK(usage.dynamic_timeout == 120);
}

TEST_CASE("analyze_list_set_usage: mixed list creates both static and dynamic sets") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);

    ListConfig cfg;
    cfg.ip_cidrs = std::vector<std::string>{"10.0.0.1"};
    cfg.domains = std::vector<std::string>{"example.com"};

    const auto usage = analyze_list_set_usage("mixed", cfg, streamer);

    CHECK(usage.has_static_entries);
    CHECK(usage.has_domain_entries);
    CHECK(usage.dynamic_timeout == 0);
}
