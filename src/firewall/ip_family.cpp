#include "ip_family.hpp"

#include "../config/list_parser.hpp"
#include "../lists/list_entry_visitor.hpp"

namespace keen_pbr3 {

std::optional<FirewallFamily> ip_family_of(std::string_view address) {
  // Try as an IP address (with or without CIDR prefix)
  if (auto family = ListParser::entry_family(EntryType::Ip, address)) {
    return family == EntryFamily::Ipv4 ? FirewallFamily::ipv4 : FirewallFamily::ipv6;
  }

  // Try as a CIDR block
  if (auto family = ListParser::entry_family(EntryType::Cidr, address)) {
    return family == EntryFamily::Ipv4 ? FirewallFamily::ipv4 : FirewallFamily::ipv6;
  }

  // Not recognized as an IP or CIDR
  return std::nullopt;
}

}  // namespace keen_pbr3
