#include <doctest/doctest.h>

#include "../src/firewall/ip_family.hpp"

namespace keen_pbr3 {

TEST_CASE("ip_family_of: IPv4 addresses") {
  CHECK(ip_family_of("1.2.3.4") == FirewallFamily::ipv4);
  CHECK(ip_family_of("10.0.0.0/8") == FirewallFamily::ipv4);
  CHECK(ip_family_of("0.0.0.0/0") == FirewallFamily::ipv4);
  CHECK(ip_family_of("1.2.3.4/32") == FirewallFamily::ipv4);
  CHECK(ip_family_of("192.168.1.1") == FirewallFamily::ipv4);
  CHECK(ip_family_of("255.255.255.255") == FirewallFamily::ipv4);
  CHECK(ip_family_of("127.0.0.1/32") == FirewallFamily::ipv4);
}

TEST_CASE("ip_family_of: IPv6 addresses") {
  CHECK(ip_family_of("2001:db8::1") == FirewallFamily::ipv6);
  CHECK(ip_family_of("2001:db8::/32") == FirewallFamily::ipv6);
  CHECK(ip_family_of("::/0") == FirewallFamily::ipv6);
  CHECK(ip_family_of("::1/128") == FirewallFamily::ipv6);
  CHECK(ip_family_of("::1") == FirewallFamily::ipv6);
  CHECK(ip_family_of("::ffff:1.2.3.4") == FirewallFamily::ipv6);
  CHECK(ip_family_of("fe80::1") == FirewallFamily::ipv6);
  CHECK(ip_family_of("ff00::1/64") == FirewallFamily::ipv6);
}

TEST_CASE("ip_family_of: Invalid IPv4 prefixes") {
  CHECK(ip_family_of("1.2.3.4/33") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4/100") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4/") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4/x") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4/0032") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4/32a") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4/-1") == std::nullopt);
}

TEST_CASE("ip_family_of: Invalid IPv6 prefixes") {
  CHECK(ip_family_of("::1/129") == std::nullopt);
  CHECK(ip_family_of("::1/256") == std::nullopt);
  CHECK(ip_family_of("::/") == std::nullopt);
  CHECK(ip_family_of("2001:db8::/x") == std::nullopt);
  CHECK(ip_family_of("2001:db8::/0128") == std::nullopt);
}

TEST_CASE("ip_family_of: Invalid formats") {
  CHECK(ip_family_of("") == std::nullopt);
  CHECK(ip_family_of("abc") == std::nullopt);
  CHECK(ip_family_of("1.2.3") == std::nullopt);
  CHECK(ip_family_of(" 1.2.3.4") == std::nullopt);
  CHECK(ip_family_of("!1.2.3.4") == std::nullopt);
  CHECK(ip_family_of("fe80::1%eth0") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4 ") == std::nullopt);
  CHECK(ip_family_of("1.2.3.4.5") == std::nullopt);
  CHECK(ip_family_of("256.1.1.1") == std::nullopt);
  CHECK(ip_family_of("gggg::1") == std::nullopt);
}

TEST_CASE("ip_family_of: Very long string") {
  std::string long_string(100, 'a');
  CHECK(ip_family_of(long_string) == std::nullopt);
}

}  // namespace keen_pbr3
