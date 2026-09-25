#include "firewall_rule.hpp"

#include "../crypto/md5.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

namespace keen_pbr3 {
namespace {

bool addr_equal(const std::string& left, const std::string& right) {
  const auto left_slash = left.rfind('/');
  const auto right_slash = right.rfind('/');
  if (left_slash == std::string::npos || right_slash == std::string::npos) {
    return left == right ||
           (left_slash != std::string::npos &&
            left.substr(left_slash + 1) ==
                (left.find(':') == std::string::npos ? "32" : "128") &&
            left.substr(0, left_slash) == right) ||
           (right_slash != std::string::npos &&
            right.substr(right_slash + 1) ==
                (right.find(':') == std::string::npos ? "32" : "128") &&
            right.substr(0, right_slash) == left);
  }
  return left == right;
}

bool addresses_equal(const std::vector<std::string>& left,
                     const std::vector<std::string>& right) {
  if (left.size() != right.size()) return false;
  return std::equal(left.begin(), left.end(), right.begin(), addr_equal);
}

bool valid_id_character(char value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '.' || value == '_' ||
         value == '-';
}

bool is_ipv6_address(const std::string& address) {
  return address.find(':') != std::string::npos;
}

bool needs_family_specific_rule(const FirewallRuleCriteria& criteria) {
  return criteria.dst_set_name.has_value() || criteria.dscp.has_value() ||
         !criteria.src_addr.empty() || !criteria.dst_addr.empty() ||
         !criteria.src_port.empty() || !criteria.dst_port.empty() ||
         criteria.default_gateway != DefaultGatewayFamily::None;
}

std::vector<FirewallFamily> materialization_families(
    const FirewallRuleInstance& rule, FirewallBackend backend) {
  if ((rule.family == FirewallFamily::ipv4 &&
       rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6) ||
      (rule.family == FirewallFamily::ipv6 &&
       rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4)) {
    return {};
  }
  if (rule.family != FirewallFamily::any) return {rule.family};
  if (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4) {
    return {FirewallFamily::ipv4};
  }
  if (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6) {
    return {FirewallFamily::ipv6};
  }
  if (backend == FirewallBackend::nftables &&
      std::holds_alternative<BalanceAction>(rule.action)) {
    return {FirewallFamily::ipv4, FirewallFamily::ipv6};
  }
  if (backend == FirewallBackend::nftables &&
      !needs_family_specific_rule(rule.criteria)) {
    return {FirewallFamily::ipv4};
  }
  return {FirewallFamily::ipv4, FirewallFamily::ipv6};
}

std::vector<L4Proto> materialization_protocols(
    const FirewallRuleCriteria& criteria, FirewallBackend backend) {
  if (criteria.proto == L4Proto::TcpUdp ||
      (backend == FirewallBackend::iptables && criteria.proto == L4Proto::Any &&
       (!criteria.src_port.empty() || !criteria.dst_port.empty()))) {
    return {L4Proto::Tcp, L4Proto::Udp};
  }
  return {criteria.proto};
}

std::vector<std::string> addresses_for_family(
    const std::vector<std::string>& addresses, FirewallFamily family) {
  std::vector<std::string> result;
  for (const auto& address : addresses) {
    if ((family == FirewallFamily::ipv6) == is_ipv6_address(address)) {
      result.push_back(address);
    }
  }
  return result;
}

std::optional<FirewallRuleAction> action_for_family(
    const FirewallRuleAction& action, FirewallFamily family,
    uint32_t fwmark_mask) {
  const auto* balance = std::get_if<BalanceAction>(&action);
  if (balance == nullptr) {
    if (std::holds_alternative<MarkAction>(action) ||
        std::holds_alternative<VerdictAction>(action)) {
      return action;
    }
    return std::nullopt;
  }

  BalanceAction filtered;
  filtered.fallback_mark = balance->fallback_mark;
  for (const auto& candidate : balance->candidates) {
    if ((family == FirewallFamily::ipv4 && candidate.ipv4) ||
        (family == FirewallFamily::ipv6 && candidate.ipv6)) {
      filtered.candidates.push_back(
          {candidate.fwmark, family == FirewallFamily::ipv4,
           family == FirewallFamily::ipv6});
    }
  }
  if (filtered.candidates.empty()) {
    return FirewallRuleAction{
        MarkAction{filtered.fallback_mark, fwmark_mask}};
  }
  if (filtered.candidates.size() == 1U) {
    return FirewallRuleAction{
        MarkAction{filtered.candidates.front().fwmark, fwmark_mask}};
  }
  // The specialized nft balance compiler owns multi-candidate forms; retain
  // the family-filtered action for the verifier's physical expectation.
  filtered.fallback_mark = 0;
  return FirewallRuleAction{std::move(filtered)};
}

void validate_id(std::string_view value, const char *name) {
  if (value.empty()) {
    throw std::invalid_argument(std::string("firewall rule ") + name +
                                " must not be empty");
  }
  if (value.size() > kFirewallRuleIdMaxLength) {
    throw std::invalid_argument(std::string("firewall rule ") + name +
                                " is too long");
  }
  for (const char character : value) {
    if (!valid_id_character(character)) {
      throw std::invalid_argument(std::string("firewall rule ") + name +
                                  " contains an invalid character");
    }
  }
}

std::string readable_comment(const FirewallRuleKey &key) {
  std::string serialized;
  serialized.reserve(kFirewallRuleCommentPrefix.size() + key.module_id.size() +
                     1U + key.instance_id.size());
  serialized += kFirewallRuleCommentPrefix;
  serialized += key.module_id;
  serialized.push_back(':');
  serialized += key.instance_id;
  return serialized;
}

} // namespace

std::vector<FirewallPhysicalClassifier> materialize_firewall_classifiers(
    const FirewallRuleInstance& rule, FirewallBackend backend,
    uint32_t fwmark_mask,
    const std::vector<std::string>* inbound_interfaces) {
  std::vector<FirewallPhysicalClassifier> result;
  for (const auto family : materialization_families(rule, backend)) {
    const auto src = addresses_for_family(rule.criteria.src_addr, family);
    const auto dst = addresses_for_family(rule.criteria.dst_addr, family);
    if ((!rule.criteria.src_addr.empty() && src.empty()) ||
        (!rule.criteria.dst_addr.empty() && dst.empty())) {
      continue;
    }

    const auto action = action_for_family(rule.action, family, fwmark_mask);
    if (!action.has_value()) continue;
    const auto protocols = materialization_protocols(rule.criteria, backend);
    const std::size_t src_count = backend == FirewallBackend::iptables
        ? std::max<std::size_t>(1U, src.size()) : 1U;
    const std::size_t dst_count = backend == FirewallBackend::iptables
        ? std::max<std::size_t>(1U, dst.size()) : 1U;
    for (const auto proto : protocols) {
      for (std::size_t src_index = 0; src_index < src_count; ++src_index) {
        for (std::size_t dst_index = 0; dst_index < dst_count; ++dst_index) {
          FirewallPhysicalClassifier physical;
          physical.family = family;
          physical.hook = rule.hook;
          physical.action = *action;
          physical.criteria = rule.criteria;
          physical.criteria.proto = proto;
          if (!rule.criteria.src_addr.empty()) {
            physical.criteria.src_addr = backend == FirewallBackend::iptables
                ? std::vector<std::string>{src[src_index]} : src;
          }
          if (!rule.criteria.dst_addr.empty()) {
            physical.criteria.dst_addr = backend == FirewallBackend::iptables
                ? std::vector<std::string>{dst[dst_index]} : dst;
          }

          const bool add_gateway_companion =
              backend == FirewallBackend::nftables &&
              rule.criteria.apply_output &&
              rule.criteria.default_gateway != DefaultGatewayFamily::None;
          if (backend == FirewallBackend::iptables &&
              inbound_interfaces != nullptr && inbound_interfaces->size() > 1U) {
            for (const auto& interface : *inbound_interfaces) {
              auto fragment = physical;
              fragment.inbound_interface = interface;
              result.push_back(std::move(fragment));
            }
          } else {
            result.push_back(physical);
          }
          if (add_gateway_companion) {
            auto companion = physical;
            companion.hook = FirewallHook::prerouting;
            companion.criteria.apply_output = false;
            result.push_back(std::move(companion));
          }
        }
      }
    }
  }
  return result;
}

std::string normalize_firewall_set_name(const std::string& name) {
  if (name.rfind("kpbr4s_", 0) == 0 || name.rfind("kpbr4S_", 0) == 0) {
    return "kpbr4_" + name.substr(7);
  }
  if (name.rfind("kpbr6s_", 0) == 0 || name.rfind("kpbr6S_", 0) == 0) {
    return "kpbr6_" + name.substr(7);
  }
  return name;
}

bool firewall_rule_criteria_equal(const FirewallRuleCriteria& left,
                                  const FirewallRuleCriteria& right) {
  const bool sets_equal = (!left.dst_set_name.has_value() &&
                           !right.dst_set_name.has_value()) ||
      (left.dst_set_name.has_value() && right.dst_set_name.has_value() &&
       normalize_firewall_set_name(*left.dst_set_name) ==
           normalize_firewall_set_name(*right.dst_set_name));
  const bool gateway_equal =
      (left.default_gateway == right.default_gateway &&
       left.default_gateway_bypass == right.default_gateway_bypass) ||
      (left.default_gateway != DefaultGatewayFamily::None &&
       right.default_gateway == DefaultGatewayFamily::None &&
       right.negate_dst_addr &&
       addresses_equal(left.default_gateway_bypass, right.dst_addr)) ||
      (right.default_gateway != DefaultGatewayFamily::None &&
       left.default_gateway == DefaultGatewayFamily::None &&
       left.negate_dst_addr &&
       addresses_equal(right.default_gateway_bypass, left.dst_addr));
  const bool destination_equal =
      addresses_equal(left.dst_addr, right.dst_addr) ||
      (left.default_gateway != DefaultGatewayFamily::None &&
       right.default_gateway == DefaultGatewayFamily::None &&
       addresses_equal(left.default_gateway_bypass, right.dst_addr)) ||
      (right.default_gateway != DefaultGatewayFamily::None &&
       left.default_gateway == DefaultGatewayFamily::None &&
       addresses_equal(right.default_gateway_bypass, left.dst_addr));
  const bool destination_negation_equal =
      left.negate_dst_addr == right.negate_dst_addr ||
      (left.default_gateway != DefaultGatewayFamily::None &&
       right.default_gateway == DefaultGatewayFamily::None &&
       right.negate_dst_addr) ||
      (right.default_gateway != DefaultGatewayFamily::None &&
       left.default_gateway == DefaultGatewayFamily::None &&
       left.negate_dst_addr);
  return sets_equal && left.dscp == right.dscp && left.proto == right.proto &&
         left.src_port.to_config_string() == right.src_port.to_config_string() &&
         left.dst_port.to_config_string() == right.dst_port.to_config_string() &&
         addresses_equal(left.src_addr, right.src_addr) && destination_equal &&
         left.negate_src_port == right.negate_src_port &&
         left.negate_dst_port == right.negate_dst_port &&
         left.negate_src_addr == right.negate_src_addr &&
         destination_negation_equal && gateway_equal;
}

std::string FirewallRuleKey::comment() const {
  validate_id(module_id, "module_id");
  validate_id(instance_id, "instance_id");

  const auto readable = readable_comment(*this);
  if (readable.size() > kFirewallRuleCommentMaxLength) {
    throw std::length_error("firewall rule comment is too long");
  }
  return readable;
}

FirewallRuleKey FirewallRuleKey::compact(
    std::string_view module_id, std::string_view semantic_instance) {
  validate_id(module_id, "module_id");
  if (semantic_instance.empty()) {
    throw std::invalid_argument(
        "firewall rule semantic_instance must not be empty");
  }

  const FirewallRuleKey semantic{std::string(module_id),
                                 std::string(semantic_instance)};
  return {semantic.module_id,
          crypto::md5_hex(readable_comment(semantic))};
}

FirewallRuleKey FirewallRuleKey::from_comment(std::string_view serialized) {
  if (serialized.size() > kFirewallRuleCommentMaxLength ||
      serialized.rfind(kFirewallRuleCommentPrefix, 0) != 0) {
    throw std::invalid_argument("invalid firewall rule comment");
  }

  serialized.remove_prefix(kFirewallRuleCommentPrefix.size());
  const auto separator = serialized.find(':');
  if (separator == std::string_view::npos ||
      serialized.find(':', separator + 1U) != std::string_view::npos) {
    throw std::invalid_argument("invalid firewall rule comment");
  }

  FirewallRuleKey key{std::string(serialized.substr(0, separator)),
                      std::string(serialized.substr(separator + 1U))};
  validate_id(key.module_id, "module_id");
  validate_id(key.instance_id, "instance_id");
  return key;
}

} // namespace keen_pbr3
