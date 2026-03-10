#include <doctest/doctest.h>

#include "../src/dns/dnsmasq_gen.hpp"

using namespace keen_pbr3;

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
